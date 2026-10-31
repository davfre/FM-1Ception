//! FM-1Ception's engines. Each handle is one emulated FM-1 on a thread of its own: upstream's
//! plug-in machine thread (crates/fm1-plugin/src/engine.rs) and its .fm1save codec, compiled
//! from the repository's crates, behind a C ABI (include/fm1ception_engine.h). A project
//! keeps each engine's whole machine (fm1-soc's save-states) besides its firmware and flash.
//!
//! Upstream reads its nih-plug parameter set from the machine thread; `Fm1Params` here is the part
//! of it the thread touches, with the parameters as atomics the plug-in sets.

use serde::{Deserialize, Serialize};
use std::cell::UnsafeCell;
use std::ffi::{c_char, CStr};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::{mpsc, Arc, Mutex};

#[path = "../../../crates/fm1-plugin/src/engine.rs"]
#[allow(dead_code)]
mod engine;
#[path = "../../../crates/fm1-plugin/src/savefile.rs"]
#[allow(dead_code)]
mod savefile;

use engine::{Cmd, Midi, Shared, View};

/// The firmware package the engine runs.
#[derive(Clone, Default, Serialize, Deserialize)]
pub struct Firmware {
    pub name: String,
    #[serde(with = "b64")]
    pub pkg: Arc<Vec<u8>>,
}

/// A flash sector the firmware wrote (its patches and settings).
#[derive(Clone, Serialize, Deserialize, PartialEq)]
pub struct Sector {
    pub at: u32,
    #[serde(with = "b64")]
    pub data: Arc<Vec<u8>>,
}

/// Bytes as base64 in a .fm1save (a package is ~600 KB).
mod b64 {
    use base64::Engine;
    use serde::{Deserialize, Deserializer, Serializer};
    use std::sync::Arc;

    pub fn serialize<S: Serializer>(v: &Arc<Vec<u8>>, s: S) -> Result<S::Ok, S::Error> {
        s.serialize_str(&base64::engine::general_purpose::STANDARD.encode(v.as_slice()))
    }

    pub fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<Arc<Vec<u8>>, D::Error> {
        let s = String::deserialize(d)?;
        base64::engine::general_purpose::STANDARD.decode(s).map(Arc::new).map_err(serde::de::Error::custom)
    }
}

pub struct Level(AtomicU32);

impl Level {
    pub fn value(&self) -> f32 {
        f32::from_bits(self.0.load(Ordering::Relaxed))
    }
}

#[derive(Clone, Copy)]
pub struct Clock(u32);

impl Clock {
    pub fn mhz(self) -> u64 {
        self.0 as u64
    }
}

pub struct ClockParam(AtomicU32);

impl ClockParam {
    pub fn value(&self) -> Clock {
        Clock(self.0.load(Ordering::Relaxed))
    }
}

pub struct Fm1Params {
    pub firmware: Mutex<Firmware>,
    pub flash: Mutex<Vec<Sector>>,
    /// The MASTER pot, 0..1.
    pub master: Level,
    /// The emulated clock. Changing it reboots the firmware, keeping its flash.
    pub clock: ClockParam,
}

impl Default for Fm1Params {
    fn default() -> Self {
        Self {
            firmware: Mutex::new(Firmware::default()),
            flash: Mutex::new(Vec::new()),
            master: Level(AtomicU32::new((900.0f32 / 1023.0).to_bits())),
            clock: ClockParam(AtomicU32::new(default_mhz(jit_available()))),
        }
    }
}

/// The clock an engine starts on: the chip's own 240 MHz. It is also the cheapest for the host,
/// which matters with eight engines: idle time is skipped, but every emulated cycle the firmware
/// does use costs host time, so with notes sounding 400 MHz costs 1.2-1.7x as much CPU as 240
/// (x0x 6.6x real time at 240 vs 4.0x at 400, omni 7.8x vs 4.8x, carnac 3.9x vs 3.1x). 320 and
/// 400 are there for a firmware that runs out of cycles.
pub fn default_mhz(_jit: bool) -> u32 {
    240
}

/// Whether this process may map JIT pages: the block compiler needs them (fm1-core's jit.rs falls
/// back to the interpreter without, as it does in a hardened host without allow-jit).
pub fn jit_available() -> bool {
    #[cfg(all(target_os = "macos", target_arch = "aarch64"))]
    {
        use std::ffi::{c_int, c_void};
        extern "C" {
            fn mmap(addr: *mut c_void, len: usize, prot: c_int, flags: c_int, fd: c_int, off: i64) -> *mut c_void;
            fn munmap(addr: *mut c_void, len: usize) -> c_int;
        }
        const MAP_FLAGS: c_int = 0x0002 | 0x1000 | 0x0800; // PRIVATE | ANON | JIT
        // SAFETY: an anonymous mapping chosen by the OS, unmapped straight away.
        unsafe {
            let p = mmap(std::ptr::null_mut(), 16384, 7, MAP_FLAGS, -1, 0);
            if p as isize == -1 || p.is_null() {
                return false;
            }
            munmap(p, 16384);
        }
        true
    }
    #[cfg(not(all(target_os = "macos", target_arch = "aarch64")))]
    {
        cfg!(any(target_os = "linux", target_os = "windows")) && cfg!(any(target_arch = "aarch64", target_arch = "x86_64"))
    }
}

/// Room for the lead at any host block size and rate the plug-in accepts.
const AUDIO_RING: usize = 1 << 15;
const MIDI_RING: usize = 4096;

pub struct Fm1e {
    shared: Arc<Shared>,
    params: Arc<Fm1Params>,
    cmds: mpsc::Sender<Cmd>,
    /// the package the machine runs, and the flash it was booted with
    running: Mutex<Option<(Arc<Vec<u8>>, Vec<Sector>)>>,
    thread: Option<std::thread::JoinHandle<()>>,
    jit: bool,
    busy: AtomicBool,
    /// the machine state fm1e_save_state took last
    state: Mutex<Vec<u8>>,
    /// how the last fm1e_boot_state went, until fm1e_restore_result reports it
    restoring: Mutex<Option<mpsc::Receiver<Result<(), String>>>>,
    // the audio thread's ends of the rings
    midi: UnsafeCell<rtrb::Producer<Midi>>,
    audio: UnsafeCell<rtrb::Consumer<[f32; 2]>>,
}

// SAFETY: the rings are only touched by fm1e_push_midi / fm1e_pop, which the header requires to
// be called from one thread at a time (the audio thread). Everything else is behind locks or
// atomics, and mpsc::Sender is Sync.
unsafe impl Sync for Fm1e {}

fn spawn() -> Fm1e {
    let shared = Arc::new(Shared {
        view: Mutex::new(View {
            lcd: vec![0; 240 * 240],
            leds: [0.0; 41],
            pads: [0; 16],
            board: fm1_soc::Board::Fm1,
            booted: false,
            halted: false,
            speed: 0.0,
            starved: 0,
            late: 0,
        }),
        quit: Default::default(),
    });
    let jit = jit_available();
    let params = Arc::new(Fm1Params::default());
    let (tx, rx) = mpsc::channel();
    let (midi_tx, midi_rx) = rtrb::RingBuffer::new(MIDI_RING);
    let (audio_tx, audio_rx) = rtrb::RingBuffer::new(AUDIO_RING);
    let (sh, pr) = (shared.clone(), params.clone());
    let thread = std::thread::Builder::new()
        .name("fm1ception-engine".into())
        .spawn(move || {
            performance_cores();
            engine::run(sh, pr, rx, midi_rx, audio_tx)
        })
        .expect("the engine's thread");
    Fm1e {
        shared,
        params,
        cmds: tx,
        running: Mutex::new(None),
        thread: Some(thread),
        jit,
        busy: AtomicBool::new(false),
        state: Mutex::new(Vec::new()),
        restoring: Mutex::new(None),
        midi: UnsafeCell::new(midi_tx),
        audio: UnsafeCell::new(audio_rx),
    }
}

/// macOS schedules a default-QoS thread on the efficiency cores when it likes; an engine is real
/// time work, so it asks for the performance cores (as fm1-live's audio thread does).
fn performance_cores() {
    #[cfg(target_os = "macos")]
    {
        extern "C" {
            fn pthread_set_qos_class_self_np(qos_class: u32, relative_priority: i32) -> i32;
        }
        const QOS_CLASS_USER_INTERACTIVE: u32 = 0x21;
        // SAFETY: changes only the calling thread's scheduling class.
        unsafe {
            pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        }
    }
}

impl Drop for Fm1e {
    fn drop(&mut self) {
        self.shared.quit.store(true, Ordering::Relaxed);
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}

const SECTOR_BLOB: usize = 4 + savefile::SECTOR_SIZE;

/// The flash blob the plug-in keeps: per sector its offset (u32, little-endian) and its bytes.
fn sectors_from_blob(blob: &[u8]) -> Option<Vec<Sector>> {
    if blob.len() % SECTOR_BLOB != 0 {
        return None;
    }
    let sectors: Vec<Sector> = blob
        .chunks_exact(SECTOR_BLOB)
        .map(|c| Sector { at: u32::from_le_bytes([c[0], c[1], c[2], c[3]]), data: Arc::new(c[4..].to_vec()) })
        .collect();
    savefile::validate_sectors(&sectors).ok()?;
    Some(sectors)
}

fn sectors_to_blob(sectors: &[Sector]) -> Vec<u8> {
    let mut out = Vec::with_capacity(sectors.len() * SECTOR_BLOB);
    for s in sectors {
        out.extend_from_slice(&s.at.to_le_bytes());
        out.extend_from_slice(&s.data);
    }
    out
}

/// Copies what fits of `src` into `out`, returning the whole length.
unsafe fn copy_out(src: &[u8], out: *mut u8, cap: usize) -> usize {
    if !out.is_null() && cap > 0 {
        std::ptr::copy_nonoverlapping(src.as_ptr(), out, src.len().min(cap));
    }
    src.len()
}

/// A message into the caller's buffer, NUL-terminated and cut at a character boundary.
unsafe fn message(msg: &str, out: *mut c_char, cap: usize) {
    if out.is_null() || cap == 0 {
        return;
    }
    let mut n = msg.len().min(cap - 1);
    while !msg.is_char_boundary(n) {
        n -= 1;
    }
    std::ptr::copy_nonoverlapping(msg.as_ptr(), out as *mut u8, n);
    *out.add(n) = 0;
}

unsafe fn path_of(path: *const c_char) -> Option<std::path::PathBuf> {
    if path.is_null() {
        return None;
    }
    Some(std::path::PathBuf::from(CStr::from_ptr(path).to_str().ok()?))
}

impl Fm1e {
    fn boot(&self, name: String, pkg: Arc<Vec<u8>>, sectors: Vec<Sector>) {
        let mut running = self.running.lock().unwrap();
        if running.as_ref().is_some_and(|(p, s)| **p == *pkg && *s == sectors) {
            return;
        }
        *self.params.firmware.lock().unwrap() = Firmware { name, pkg: pkg.clone() };
        *self.params.flash.lock().unwrap() = sectors.clone();
        let raw = sectors.iter().map(|s| (s.at, s.data.to_vec())).collect();
        *running = (!pkg.is_empty()).then(|| (pkg.clone(), sectors));
        let _ = self.cmds.send(Cmd::Boot { pkg, sectors: raw });
    }

    /// Boot with a saved machine in place of the fresh one (even if this firmware and flash run).
    fn boot_state(&self, name: String, pkg: Arc<Vec<u8>>, sectors: Vec<Sector>, state: Vec<u8>) {
        let mut running = self.running.lock().unwrap();
        *self.params.firmware.lock().unwrap() = Firmware { name, pkg: pkg.clone() };
        *self.params.flash.lock().unwrap() = sectors.clone();
        let raw = sectors.iter().map(|s| (s.at, s.data.to_vec())).collect();
        *running = (!pkg.is_empty()).then(|| (pkg.clone(), sectors));
        let (tx, rx) = mpsc::channel();
        *self.restoring.lock().unwrap() = Some(rx);
        let _ = self.cmds.send(Cmd::Restore { pkg, sectors: raw, state: Arc::new(state), reply: tx });
    }

    /// The whole machine now, and the flash it goes with (which fm1e_flash gives from then on).
    fn save_state(&self) -> Result<String, String> {
        let (tx, rx) = mpsc::channel();
        self.cmds.send(Cmd::SaveState { reply: tx }).map_err(|_| "the engine stopped".to_owned())?;
        let (_, state) = rx
            .recv_timeout(std::time::Duration::from_secs(5))
            .map_err(|_| "the engine did not answer".to_owned())??;
        *self.state.lock().unwrap() = state;
        Ok(String::new())
    }

    fn load(&self, path: &std::path::Path) -> Result<String, String> {
        let current = self.params.firmware.lock().unwrap().clone();
        let data = savefile::read_with_firmware(path, Some(&current))?;
        let firmware = data.firmware.clone();
        let sectors = data.sectors.clone();
        let (tx, rx) = mpsc::channel();
        self.cmds
            .send(Cmd::Load { firmware: data.firmware, sectors: data.sectors, reply: tx })
            .map_err(|_| "the engine stopped".to_owned())?;
        rx.recv().map_err(|_| "the engine stopped while loading".to_owned())??;
        *self.running.lock().unwrap() = Some((firmware.pkg, sectors));
        Ok(format!("Loaded {}", firmware.name))
    }

    /// The live flash, once a save in progress has settled (the copy fm1e_flash gives is refreshed
    /// from it every two seconds; this refreshes it now).
    fn capture(&self) -> Result<(Firmware, Vec<Sector>), String> {
        let firmware = self.params.firmware.lock().unwrap().clone();
        if firmware.pkg.is_empty() {
            return Err("load a firmware first".into());
        }
        let (tx, rx) = mpsc::channel();
        self.cmds
            .send(Cmd::Snapshot { pkg: firmware.pkg.clone(), reply: tx })
            .map_err(|_| "the engine stopped".to_owned())?;
        let sectors = rx.recv().map_err(|_| "the engine stopped while saving".to_owned())??;
        Ok((firmware, sectors))
    }

    fn export(&self, mut path: std::path::PathBuf) -> Result<String, String> {
        if path.extension().is_none() {
            path.set_extension("fm1save");
        }
        let (firmware, sectors) = self.capture()?;
        savefile::write(&path, &savefile::SaveData { firmware, sectors })?;
        Ok(format!("Exported {}", path.file_name().unwrap_or_default().to_string_lossy()))
    }

    /// One file job at a time per engine: a second waits for the first's reply otherwise.
    fn exclusive(&self, job: impl FnOnce() -> Result<String, String>) -> Result<String, String> {
        if self.busy.swap(true, Ordering::Acquire) {
            return Err("this engine is busy with another file".into());
        }
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(job))
            .unwrap_or_else(|_| Err("the file could not be read".into()));
        self.busy.store(false, Ordering::Release);
        result
    }
}

// ------------------------------------------------------------------------------------- C ABI

#[repr(C)]
pub struct Fm1eStatus {
    pub booted: u8,
    pub halted: u8,
    pub smk37: u8,
    pub jit: u8,
    pub speed: f32,
    pub late: u64,
}

#[no_mangle]
pub extern "C" fn fm1e_create() -> *mut Fm1e {
    std::panic::catch_unwind(|| Box::into_raw(Box::new(spawn()))).unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// `e` from fm1e_create, not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn fm1e_destroy(e: *mut Fm1e) {
    if !e.is_null() {
        drop(Box::from_raw(e));
    }
}

#[no_mangle]
pub extern "C" fn fm1e_jit_available() -> bool {
    jit_available()
}

#[no_mangle]
pub extern "C" fn fm1e_default_mhz() -> u32 {
    default_mhz(jit_available())
}

/// # Safety
/// `e` a live handle.
#[no_mangle]
pub unsafe extern "C" fn fm1e_set_lead(e: *const Fm1e, frames: u32) {
    let e = &*e;
    let _ = e.cmds.send(Cmd::Lead((frames as usize).clamp(64, AUDIO_RING - 4096)));
}

/// # Safety
/// `e` a live handle.
#[no_mangle]
pub unsafe extern "C" fn fm1e_set_clock(e: *const Fm1e, mhz: u32) {
    (&*e).params.clock.0.store(mhz.clamp(40, 800), Ordering::Relaxed);
}

/// # Safety
/// `e` a live handle.
#[no_mangle]
pub unsafe extern "C" fn fm1e_set_master(e: *const Fm1e, value: f32) {
    (&*e).params.master.0.store(value.clamp(0.0, 1.0).to_bits(), Ordering::Relaxed);
}

/// Boot a package with the flash blob saved from an earlier run (unless it already runs that).
/// Returns 0, or -1 if the blob is malformed (nothing booted).
///
/// # Safety
/// `e` a live handle; `name` NUL-terminated (or null); the buffers valid for their lengths.
#[no_mangle]
pub unsafe extern "C" fn fm1e_boot(e: *const Fm1e, name: *const c_char, pkg: *const u8, pkg_len: usize,
                                   flash: *const u8, flash_len: usize) -> i32 {
    let e = &*e;
    let blob = if flash.is_null() { &[][..] } else { std::slice::from_raw_parts(flash, flash_len) };
    let Some(sectors) = sectors_from_blob(blob) else { return -1 };
    let pkg = if pkg.is_null() { Vec::new() } else { std::slice::from_raw_parts(pkg, pkg_len).to_vec() };
    let name = if name.is_null() { String::new() } else { CStr::from_ptr(name).to_string_lossy().into_owned() };
    e.boot(name, Arc::new(pkg), sectors);
    0
}

/// Stop the firmware and forget it and its flash: the engine is blank again.
///
/// # Safety
/// `e` a live handle.
#[no_mangle]
pub unsafe extern "C" fn fm1e_unload(e: *const Fm1e) {
    (&*e).boot(String::new(), Arc::new(Vec::new()), Vec::new());
}

/// Open a .fwsc package or a .fm1save setup, waiting for the engine to boot it. Blocks: call it
/// off the message and audio threads. Returns 0 (msg: what happened) or -1 (msg: why not).
///
/// # Safety
/// `e` a live handle; `path` NUL-terminated UTF-8; `msg` writable for `cap` bytes (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_load_file(e: *const Fm1e, path: *const c_char, msg: *mut c_char, cap: usize) -> i32 {
    let e = &*e;
    let result = match path_of(path) {
        Some(p) => e.exclusive(|| e.load(&p)),
        None => Err("not a file path".into()),
    };
    let ok = result.is_ok();
    message(&result.unwrap_or_else(|x| x), msg, cap);
    if ok { 0 } else { -1 }
}

/// Write the running firmware and its live flash as a .fm1save. Blocks, as fm1e_load_file.
///
/// # Safety
/// As fm1e_load_file.
#[no_mangle]
pub unsafe extern "C" fn fm1e_export_file(e: *const Fm1e, path: *const c_char, msg: *mut c_char, cap: usize) -> i32 {
    let e = &*e;
    let result = match path_of(path) {
        Some(p) => e.exclusive(|| e.export(p)),
        None => Err("not a file path".into()),
    };
    let ok = result.is_ok();
    message(&result.unwrap_or_else(|x| x), msg, cap);
    if ok { 0 } else { -1 }
}

/// Bring fm1e_flash's copy up to the live flash, waiting for a save in progress to settle (up to
/// 5 s). Blocks, as fm1e_load_file. Returns 0, or -1 (msg: why not).
///
/// # Safety
/// `e` a live handle; `msg` writable for `cap` bytes (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_capture_flash(e: *const Fm1e, msg: *mut c_char, cap: usize) -> i32 {
    let e = &*e;
    let result = e.exclusive(|| {
        let (firmware, sectors) = e.capture()?;
        if *e.params.firmware.lock().unwrap().pkg == *firmware.pkg {
            *e.params.flash.lock().unwrap() = sectors;
        }
        Ok(String::new())
    });
    let ok = result.is_ok();
    message(&result.unwrap_or_else(|x| x), msg, cap);
    if ok { 0 } else { -1 }
}

/// Take the whole machine as it is now, for fm1e_state, and bring fm1e_flash's copy up to the flash
/// it goes with. Blocks (a few ms; up to 5 s), as fm1e_load_file. Returns 0, or -1 (msg: why not:
/// no firmware runs, or it has stopped).
///
/// # Safety
/// `e` a live handle; `msg` writable for `cap` bytes (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_save_state(e: *const Fm1e, msg: *mut c_char, cap: usize) -> i32 {
    let e = &*e;
    let result = e.exclusive(|| e.save_state());
    let ok = result.is_ok();
    message(&result.unwrap_or_else(|x| x), msg, cap);
    if ok { 0 } else { -1 }
}

/// The machine state fm1e_save_state took last; returns its length (0: none).
///
/// # Safety
/// `e` a live handle; `out` writable for `cap` bytes (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_state(e: *const Fm1e, out: *mut u8, cap: usize) -> usize {
    copy_out(&(&*e).state.lock().unwrap(), out, cap)
}

/// fm1e_boot, with a machine state from fm1e_state put in place of the fresh boot. If the state
/// does not fit the package and flash (or is damaged), they boot as fm1e_boot would; how it went
/// comes from fm1e_restore_result. Returns 0, or -1 if the flash blob is malformed (nothing booted).
///
/// # Safety
/// As fm1e_boot; `state` valid for `state_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn fm1e_boot_state(e: *const Fm1e, name: *const c_char, pkg: *const u8, pkg_len: usize,
                                         flash: *const u8, flash_len: usize, state: *const u8, state_len: usize) -> i32 {
    let e = &*e;
    let blob = if flash.is_null() { &[][..] } else { std::slice::from_raw_parts(flash, flash_len) };
    let Some(sectors) = sectors_from_blob(blob) else { return -1 };
    let pkg = if pkg.is_null() { Vec::new() } else { std::slice::from_raw_parts(pkg, pkg_len).to_vec() };
    let name = if name.is_null() { String::new() } else { CStr::from_ptr(name).to_string_lossy().into_owned() };
    let state = if state.is_null() { Vec::new() } else { std::slice::from_raw_parts(state, state_len).to_vec() };
    e.boot_state(name, Arc::new(pkg), sectors, state);
    0
}

/// How the last fm1e_boot_state went, once: 0 the machine came back, -1 it booted afresh instead
/// (msg: why), 1 nothing (yet) to report.
///
/// # Safety
/// `e` a live handle; `msg` writable for `cap` bytes (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_restore_result(e: *const Fm1e, msg: *mut c_char, cap: usize) -> i32 {
    let mut restoring = (&*e).restoring.lock().unwrap();
    let Some(rx) = restoring.as_ref() else { return 1 };
    let result = match rx.try_recv() {
        Ok(r) => r,
        Err(mpsc::TryRecvError::Empty) => return 1,
        Err(mpsc::TryRecvError::Disconnected) => Err("the engine stopped".into()),
    };
    *restoring = None;
    match result {
        Ok(()) => 0,
        Err(why) => {
            message(&why, msg, cap);
            -1
        }
    }
}

/// The firmware's name (UTF-8, not terminated); returns its length.
///
/// # Safety
/// `e` a live handle; `out` writable for `cap` bytes (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_name(e: *const Fm1e, out: *mut u8, cap: usize) -> usize {
    copy_out((&*e).params.firmware.lock().unwrap().name.as_bytes(), out, cap)
}

/// The package the engine runs; returns its length (0: none).
///
/// # Safety
/// As fm1e_name.
#[no_mangle]
pub unsafe extern "C" fn fm1e_package(e: *const Fm1e, out: *mut u8, cap: usize) -> usize {
    let pkg = (&*e).params.firmware.lock().unwrap().pkg.clone();
    copy_out(&pkg, out, cap)
}

/// The flash the firmware has written, as fm1e_boot takes it, refreshed every two seconds.
///
/// # Safety
/// As fm1e_name.
#[no_mangle]
pub unsafe extern "C" fn fm1e_flash(e: *const Fm1e, out: *mut u8, cap: usize) -> usize {
    let blob = sectors_to_blob(&(&*e).params.flash.lock().unwrap());
    copy_out(&blob, out, cap)
}

/// A panel button or key (the matrix id: 0 OCT-, 1 OCT+, 2..13 FX..REC, 14..40 the keys).
///
/// # Safety
/// `e` a live handle.
#[no_mangle]
pub unsafe extern "C" fn fm1e_key(e: *const Fm1e, id: u8, down: bool) {
    let _ = (&*e).cmds.send(Cmd::Key { id, down });
}

/// An encoder (the matrix encoder) turned by `clicks`.
///
/// # Safety
/// `e` a live handle.
#[no_mangle]
pub unsafe extern "C" fn fm1e_enc(e: *const Fm1e, enc: u8, clicks: i32) {
    let _ = (&*e).cmds.send(Cmd::Enc { enc, clicks });
}

/// The screen (240 x 240 RGB565), the LEDs (41, the lit fraction) and the status.
///
/// # Safety
/// `e` a live handle; `lcd` writable for 57600 values, `leds` for 41 (either may be null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_view(e: *const Fm1e, lcd: *mut u16, leds: *mut f32, status: *mut Fm1eStatus) {
    let e = &*e;
    let v = e.shared.view.lock().unwrap();
    if !lcd.is_null() {
        std::ptr::copy_nonoverlapping(v.lcd.as_ptr(), lcd, v.lcd.len().min(240 * 240));
    }
    if !leds.is_null() {
        std::ptr::copy_nonoverlapping(v.leds.as_ptr(), leds, 41);
    }
    if !status.is_null() {
        *status = Fm1eStatus {
            booted: v.booted as u8,
            halted: v.halted as u8,
            smk37: (v.board == fm1_soc::Board::Smk37) as u8,
            jit: e.jit as u8,
            speed: v.speed,
            late: v.late,
        };
    }
}

/// A channel message for the MIDI IN jack, due at `frame` of the engine's 44.1 kHz output.
///
/// # Safety
/// `e` a live handle; the audio thread's (one thread at a time).
#[no_mangle]
pub unsafe extern "C" fn fm1e_push_midi(e: *const Fm1e, frame: u64, b0: u8, b1: u8, b2: u8, len: u8) -> bool {
    (*(&*e).midi.get()).push(Midi { frame, bytes: [b0, b1, b2], len: len.min(3) }).is_ok()
}

/// The next 44.1 kHz stereo frame, if the engine has made it.
///
/// # Safety
/// As fm1e_push_midi; `lr` writable for two floats.
#[no_mangle]
pub unsafe extern "C" fn fm1e_pop(e: *const Fm1e, lr: *mut f32) -> bool {
    match (*(&*e).audio.get()).pop() {
        Ok(f) => {
            *lr = f[0];
            *lr.add(1) = f[1];
            true
        }
        Err(_) => false,
    }
}

/// Frames the host found missing, for the status's "late" count.
///
/// # Safety
/// `e` a live handle.
#[no_mangle]
pub unsafe extern "C" fn fm1e_starved(e: *const Fm1e, frames: u64) {
    if let Ok(mut v) = (&*e).shared.view.try_lock() {
        v.starved += frames;
    }
}

/// A fresh boot of a package (its factory flash, no host, no audio taken), run flat out: its screen
/// when its own clock reaches each of `at_ms` (ascending), as engine.rs's Machine boots and runs it.
/// Fewer screens than asked for if it stops (or `limit` of wall time runs out) after the first.
fn screenshots(pkg: &[u8], at_ms: &[u32], limit: std::time::Duration) -> Result<Vec<Vec<u16>>, String> {
    use fm1_core::{Bus, Cpu};
    use fm1_soc::{machine, Soc};
    const RAM_BASE: u32 = 0x01c0_0000;
    const RAM_SIZE: usize = 1 << 20;
    let st = fm1_soc::stock_from_package(pkg, None)?;
    let mut soc = Soc::new(RAM_SIZE, st.xip);
    soc.periph.hz = default_mhz(jit_available()) as u64 * 1_000_000;
    soc.periph.skip_idle = true;
    let mut cpu = Cpu::new(st.entry);
    cpu.jit_on = jit_available();
    cpu.user = true;
    let params = RAM_BASE + 0x7fc00;
    soc.write32(params, params + 0x40);
    cpu.regs[0] = fm1_soc::stock_boot(&mut soc, &st.flash, params, st.board);
    let (jit, mut cpu1, mut pending) = (cpu.jit_on, None, false);
    let t0 = std::time::Instant::now();
    let mut shots = Vec::with_capacity(at_ms.len());
    for &at in at_ms {
        while soc.periph.ms() < at as u64 {
            let stopped = machine::run_dual(&mut cpu1, &mut soc, 20_000, RAM_SIZE, jit, |soc, k| {
                machine::run_blocks(&mut cpu, soc, k, &mut pending)
            });
            soc.periph.audio.clear();
            if let Err(why) = stopped {
                return if shots.is_empty() { Err(format!("the firmware stopped: {why}")) } else { Ok(shots) };
            }
            if t0.elapsed() > limit {
                return if shots.is_empty() { Err("the firmware ran too slowly to capture".into()) } else { Ok(shots) };
            }
        }
        shots.push(soc.lcd.fb.clone());
    }
    Ok(shots)
}

/// Boot a package fresh and take its screen (240 x 240 RGB565) when its clock reaches each of the
/// `n` times in `at_ms` (ascending), into `lcd` one after another. Blocks for a second or so (at
/// most about 30 s): a background thread's, the engines' own threads untouched. Returns the screens
/// taken (fewer than `n` if the firmware stopped after the first), or -1 (msg: why none).
///
/// # Safety
/// `pkg` readable for `len` bytes, `at_ms` for `n`; `lcd` writable for `n` * 57600 values; `msg`
/// writable for `cap` bytes (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_screenshots(pkg: *const u8, len: usize, at_ms: *const u32, n: usize, lcd: *mut u16,
                                          msg: *mut c_char, cap: usize) -> i32 {
    if pkg.is_null() || at_ms.is_null() || lcd.is_null() {
        message("nothing to capture", msg, cap);
        return -1;
    }
    let (bytes, at) = (std::slice::from_raw_parts(pkg, len), std::slice::from_raw_parts(at_ms, n));
    let result = std::panic::catch_unwind(|| screenshots(bytes, at, std::time::Duration::from_secs(30)))
        .unwrap_or_else(|_| Err("the firmware could not be run".into()));
    match result {
        Ok(shots) => {
            for (i, s) in shots.iter().enumerate() {
                std::ptr::copy_nonoverlapping(s.as_ptr(), lcd.add(i * 240 * 240), s.len().min(240 * 240));
            }
            shots.len() as i32
        }
        Err(why) => {
            message(&why, msg, cap);
            -1
        }
    }
}

/// What the firmware library shows of a package without booting it.
#[derive(Debug, Default, PartialEq)]
struct Probe {
    identity: String,
    name: String,
}

fn probe(pkg: &[u8]) -> Result<Probe, String> {
    let u = fm1_soc::fwsc::unpack(pkg)?;
    let name = stock_name(&u.identity).or_else(|| banner(&u.xip)).unwrap_or_default();
    Ok(Probe { identity: u.identity, name })
}

/// M-VAVE's own releases number from 0 (`FM-1_015`: "FM-1 Stock V15"); community firmwares
/// pick numbers of their own (SHRUB's 9XY, RB338's 338).
fn stock_name(identity: &str) -> Option<String> {
    let (model, number) = identity.rsplit_once('_')?;
    if model.is_empty() || number.len() < 2 || !number.starts_with('0') || !number.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    Some(format!("{model} Stock V{}", number.parse::<u32>().ok()?))
}

/// The first string (or line of one) in the code that reads as a title and a dotted version, as a
/// firmware's splash or about text names it ("SHRUB V0.4\n\nAUDIO ...").
fn banner(code: &[u8]) -> Option<String> {
    let mut start = 0;
    for (i, &b) in code.iter().enumerate() {
        if b == 0 || b == b'\n' || b == b'\r' {
            if let Some(s) = titled(&code[start..i]) {
                return Some(s);
            }
            start = i + 1;
        } else if !(0x20..0x7f).contains(&b) {
            start = i + 1;
        }
    }
    None
}

fn titled(s: &[u8]) -> Option<String> {
    if !(5..=40).contains(&s.len()) {
        return None;
    }
    let s = std::str::from_utf8(s).ok()?;
    let (title, version) = s.rsplit_once(" V").or_else(|| s.rsplit_once(" v"))?;
    let title_ok = title.starts_with(|c: char| c.is_ascii_alphabetic())
        && title.chars().filter(|c| c.is_ascii_alphabetic()).count() >= 2
        && title.chars().all(|c| c.is_ascii_alphanumeric() || " -_.".contains(c));
    let version_ok = version.starts_with(|c: char| c.is_ascii_digit())
        && version.ends_with(|c: char| c.is_ascii_digit())
        && version.contains('.')
        && version.chars().all(|c| c.is_ascii_digit() || c == '.');
    (title_ok && version_ok).then(|| s.to_owned())
}

/// A package's identity, its name and a digest of its bytes, for the firmware library. Pure: any
/// thread.
///
/// # Safety
/// `pkg` readable for `len` bytes; each out buffer writable for its capacity (or null).
#[no_mangle]
pub unsafe extern "C" fn fm1e_probe_package(pkg: *const u8, len: usize, identity: *mut c_char, identity_cap: usize,
                                            name: *mut c_char, name_cap: usize, digest: *mut c_char,
                                            digest_cap: usize) -> i32 {
    use sha2::Digest;
    let bytes = if pkg.is_null() { &[][..] } else { std::slice::from_raw_parts(pkg, len) };
    let hash = sha2::Sha256::digest(bytes);
    let hex: String = hash[..16].iter().map(|b| format!("{b:02x}")).collect();
    message(&hex, digest, digest_cap);
    let result = std::panic::catch_unwind(|| probe(bytes)).unwrap_or_else(|_| Err("not an FM-1 package".into()));
    match result {
        Ok(p) => {
            message(&p.identity, identity, identity_cap);
            message(&p.name, name, name_cap);
            0
        }
        Err(why) => {
            message("", identity, identity_cap);
            message(&why, name, name_cap);
            -1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn flash_blob_round_trip_and_rejects() {
        let s = vec![Sector { at: 0xff000, data: Arc::new(vec![7; 4096]) }];
        assert!(sectors_from_blob(&sectors_to_blob(&s)).unwrap() == s);
        assert!(sectors_from_blob(&[0; 5]).is_none());
        let mut bad = sectors_to_blob(&s);
        bad[0] = 1; // not on a sector boundary
        assert!(sectors_from_blob(&bad).is_none());
        assert!(sectors_from_blob(&[]).unwrap().is_empty());
    }

    #[test]
    fn a_package_names_itself_by_its_banner_or_its_stock_release() {
        assert_eq!(stock_name("FM-1_015").as_deref(), Some("FM-1 Stock V15"));
        assert_eq!(stock_name("SMK-37 Pro_016").as_deref(), Some("SMK-37 Pro Stock V16"));
        assert_eq!(stock_name("FM-1_901"), None);
        assert_eq!(stock_name("FM-1_338"), None);

        let code = b"\x01\x02junk\0%s V%d.%d\0BT V2\0FM-1_901\0SHRUB V0.4\n\nAUDIO \0OTHER V1.0\0";
        assert_eq!(banner(code).as_deref(), Some("SHRUB V0.4"));
        assert_eq!(banner(b"\x7fMy Synth v1.2.3\0").as_deref(), Some("My Synth v1.2.3"));
        assert_eq!(banner(b"OpenRB-338\0RB338 CRASH\0"), None);
        assert!(probe(&[0; 4096]).is_err());
    }

    /// FM1C_TEST_FIRMWARE=path/to.fwsc cargo test probes -- --nocapture
    #[test]
    fn probes_a_real_package() {
        let Ok(path) = std::env::var("FM1C_TEST_FIRMWARE") else { return };
        let p = probe(&std::fs::read(path).unwrap()).unwrap();
        println!("{p:?}");
        assert!(p.identity.starts_with("FM-1_"));
    }

    /// An engine as a host drives it (512-frame blocks at 48 kHz), from boot, a C4 held from
    /// FM1C_BENCH_NOTE seconds (default 3), one line a second:
    /// FM1C_BENCH=path/to.fwsc [FM1C_BENCH_SECS=10] cargo test --release bench -- --ignored --nocapture
    #[test]
    #[ignore]
    fn bench() {
        let Ok(path) = std::env::var("FM1C_BENCH") else { return eprintln!("set FM1C_BENCH") };
        let env_f = |k: &str, d: f64| std::env::var(k).ok().and_then(|v| v.parse().ok()).unwrap_or(d);
        let (note_s, secs) = (env_f("FM1C_BENCH_NOTE", 3.0), env_f("FM1C_BENCH_SECS", 10.0));
        const LEAD: u64 = 1495;
        let per_block = 512.0 * 44_100.0 / 48_000.0;
        let e = fm1e_create();
        unsafe {
            fm1e_set_lead(e, LEAD as u32);
            let path = std::ffi::CString::new(path).unwrap();
            assert_eq!(fm1e_load_file(e, path.as_ptr(), std::ptr::null_mut(), 0), 0);
            let (mut lr, mut st) = ([0f32; 2], std::mem::zeroed::<Fm1eStatus>());
            let (t0, mut taken, mut frac) = (std::time::Instant::now(), 0u64, 0.0f64);
            let (mut missing, mut peak) = (0u64, 0f32);
            let blocks = (secs * 48_000.0 / 512.0) as u32;
            let note_block = (note_s * 48_000.0 / 512.0) as u32;
            for block in 0..blocks {
                if block == note_block {
                    fm1e_push_midi(e, taken + LEAD, 0x90, 60, 110, 3);
                }
                frac += per_block;
                while frac >= 1.0 {
                    if !fm1e_pop(e, lr.as_mut_ptr()) {
                        missing += frac as u64;
                        frac = 0.0;
                        break;
                    }
                    taken += 1;
                    frac -= 1.0;
                    peak = peak.max(lr[0].abs());
                }
                if (block + 1) % 94 == 0 {
                    fm1e_view(e, std::ptr::null_mut(), std::ptr::null_mut(), &mut st);
                    eprintln!("{:5.1} s  speed {:5.2}x  missing {missing:6}  peak {peak:.3}",
                              (block + 1) as f64 * 512.0 / 48_000.0, st.speed);
                    (missing, peak) = (0, 0.0);
                }
                let due = std::time::Duration::from_secs_f64((block + 1) as f64 * 512.0 / 48_000.0);
                if let Some(wait) = due.checked_sub(t0.elapsed()) {
                    std::thread::sleep(wait)
                }
            }
            fm1e_destroy(e);
        }
    }

    /// MIDI timing as the plug-in stamps it: 512-frame blocks at 48 kHz in real time, a short note
    /// every half second at a position in its block that moves on each time, and each note's
    /// latency (onset frame - stamp). The spread is the jitter; one line per note, then a summary:
    /// FM1C_TIMING=path/to.fwsc [FM1C_TIMING_NOTES=40] cargo test --release midi_timing -- --ignored --nocapture
    #[test]
    #[ignore]
    fn midi_timing() {
        let Ok(path) = std::env::var("FM1C_TIMING") else { return eprintln!("set FM1C_TIMING") };
        let notes: u64 = std::env::var("FM1C_TIMING_NOTES").ok().and_then(|v| v.parse().ok()).unwrap_or(40);
        const LEAD: u64 = 1495;
        let ratio = 44_100.0 / 48_000.0;
        let e = fm1e_create();
        unsafe {
            fm1e_set_lead(e, LEAD as u32);
            let path = std::ffi::CString::new(path).unwrap();
            assert_eq!(fm1e_load_file(e, path.as_ptr(), std::ptr::null_mut(), 0), 0);
            let mut lr = [0f32; 2];
            let (t0, mut taken, mut frac, mut missing) = (std::time::Instant::now(), 0u64, 0.0f64, 0u64);
            let mut out: Vec<f32> = Vec::new();
            let mut stamps = Vec::new();
            let start_block = 8 * 48_000 / 512; // after the boot
            let (gap, off_after) = (24_000u64, 3_000u64); // host samples: a note every 0.5 s, 62 ms long
            let first = start_block as u64 * 512;
            let blocks = (first + (notes + 1) * gap) / 512 + 1;
            for block in 0..blocks {
                let (b0, b1) = (block * 512, block * 512 + 512);
                let start = taken;
                for n in 0..notes {
                    // the note's host sample: its position in the block moves 101 samples a note
                    let on = first + n * gap + n * 101;
                    for (at, status, vel) in [(on, 0x90u8, 110u8), (on + off_after, 0x80, 0)] {
                        if (b0..b1).contains(&at) {
                            let frame = start + ((at - b0) as f64 * ratio) as u64 + LEAD;
                            fm1e_push_midi(e, frame, status, 60, vel, 3);
                            if status == 0x90 {
                                stamps.push(frame);
                            }
                        }
                    }
                }
                frac += 512.0 * ratio;
                while frac >= 1.0 {
                    if !fm1e_pop(e, lr.as_mut_ptr()) {
                        missing += 1;
                        frac = frac.min(1.0);
                        break;
                    }
                    taken += 1;
                    frac -= 1.0;
                    out.push(lr[0].abs().max(lr[1].abs()));
                }
                let due = std::time::Duration::from_secs_f64((block + 1) as f64 * 512.0 / 48_000.0);
                if let Some(wait) = due.checked_sub(t0.elapsed()) {
                    std::thread::sleep(wait)
                }
            }
            fm1e_destroy(e);

            let mut lat = Vec::new();
            for &s in &stamps {
                let s = s as usize;
                let quiet = s >= 2000 && out[s - 2000..s].iter().all(|&x| x < 1.0e-3);
                let onset = (s..(s + 8000).min(out.len())).find(|&i| out[i] > 3.0e-3);
                match (quiet, onset) {
                    (true, Some(i)) => {
                        eprintln!("stamp {s:8}  latency {:5} frames", i - s);
                        lat.push((i - s) as i64);
                    }
                    _ => eprintln!("stamp {s:8}  (not silent before, or no onset)"),
                }
            }
            assert!(!lat.is_empty(), "no onsets measured");
            let (lo, hi) = (*lat.iter().min().unwrap(), *lat.iter().max().unwrap());
            let mean = lat.iter().sum::<i64>() as f64 / lat.len() as f64;
            eprintln!("{} notes: latency {lo}..{hi} frames ({:.2}..{:.2} ms), mean {mean:.1}, spread {} frames ({:.2} ms), {missing} frames missing",
                      lat.len(), lo as f64 / 44.1, hi as f64 / 44.1, hi - lo, (hi - lo) as f64 / 44.1);
        }
    }
}
