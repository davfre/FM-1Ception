//! Save-states: a whole machine between two runs (its cores and the chip), kept as what differs
//! from the same package and flash booted afresh, which whoever restores it boots again.
//!
//! The memories (RAM, the overlay and SRAM banks, the XIP window, the flash chip, the screen) go
//! as their bytes XORed with the fresh boot's, so what the boot gives anyway (the flash and the
//! package's window, RAM still clear) is zeros; the rest of the chip goes as its serde value, and
//! all of it deflated. A SHRUB session comes to about 40 KB, its machine to about 4.5 MB.
//!
//! The fresh boot has to be the same one at both ends: a fingerprint of its memories goes with
//! the state, and a state taken against another boot is refused.

use crate::{Ram, Soc};
use fm1_core::{CoreState, Cpu};
use serde::{Deserialize, Serialize};
use std::borrow::Cow;

const MAGIC: &[u8; 8] = b"FM1STATE";
/// Bumped when what is saved changes: a state of another layout is refused.
const FORMAT: u32 = 1;
/// magic, format, the fresh boot's fingerprint and clock
const HEADER: usize = 8 + 4 + 8 + 8;

/// A machine as the runner (`machine::run_dual` with `run_blocks`) leaves it between two runs.
pub struct Machine {
    pub cpu: Cpu,
    pub cpu1: Option<Cpu>,
    pub soc: Soc,
    pub pending_tick: bool,
}

#[derive(Serialize)]
struct RestOut<'a> {
    cpu: CoreState,
    cpu1: Option<CoreState>,
    pending_tick: bool,
    soc: &'a Soc,
}

#[derive(Deserialize)]
struct RestIn {
    cpu: CoreState,
    cpu1: Option<CoreState>,
    pending_tick: bool,
    soc: Soc,
}

fn memories(soc: &Soc) -> [Cow<'_, [u8]>; 6] {
    [
        Cow::Borrowed(soc.ram.bytes()),
        Cow::Borrowed(soc.ovl.bytes()),
        Cow::Borrowed(soc.sram2.bytes()),
        Cow::Borrowed(&soc.xip),
        Cow::Borrowed(&soc.flash.mem),
        Cow::Owned(soc.lcd.fb.iter().flat_map(|p| p.to_le_bytes()).collect()),
    ]
}

fn fingerprint(mems: &[Cow<'_, [u8]>]) -> u64 {
    let mut h = 0xcbf2_9ce4_8422_2325u64;
    for m in mems {
        h = (h ^ m.len() as u64).wrapping_mul(0x100_0000_01b3);
        let words = m.chunks_exact(8);
        let tail = words.remainder();
        for w in words {
            h = (h ^ u64::from_le_bytes(w.try_into().unwrap())).wrapping_mul(0x100_0000_01b3);
        }
        for &b in tail {
            h = (h ^ b as u64).wrapping_mul(0x100_0000_01b3);
        }
    }
    h
}

fn put_u32(out: &mut Vec<u8>, v: usize) {
    out.extend_from_slice(&(v as u32).to_le_bytes());
}

fn get_u32(raw: &[u8], at: &mut usize) -> Result<usize, String> {
    let b = raw.get(*at..*at + 4).ok_or("the state is cut short")?;
    *at += 4;
    Ok(u32::from_le_bytes(b.try_into().unwrap()) as usize)
}

/// The machine, against `fresh`: the same package and flash booted afresh, not yet run (what
/// `restore` is given again).
pub fn save(cpu: &Cpu, cpu1: Option<&Cpu>, soc: &Soc, pending_tick: bool, fresh: &Soc) -> Result<Vec<u8>, String> {
    let rest = RestOut { cpu: cpu.core_state(), cpu1: cpu1.map(Cpu::core_state), pending_tick, soc };
    let rest = bincode::serialize(&rest).map_err(|e| format!("the machine could not be saved: {e}"))?;
    let (now, then) = (memories(soc), memories(fresh));
    let mut raw = Vec::with_capacity(4 + rest.len() + now.iter().map(|m| 4 + m.len()).sum::<usize>());
    put_u32(&mut raw, rest.len());
    raw.extend_from_slice(&rest);
    for (a, b) in now.iter().zip(&then) {
        put_u32(&mut raw, a.len());
        raw.extend(a.iter().enumerate().map(|(i, &x)| x ^ b.get(i).copied().unwrap_or(0)));
    }

    let mut out = Vec::with_capacity(HEADER + raw.len() / 64);
    out.extend_from_slice(MAGIC);
    out.extend_from_slice(&FORMAT.to_le_bytes());
    out.extend_from_slice(&fingerprint(&then).to_le_bytes());
    out.extend_from_slice(&fresh.periph.hz.to_le_bytes());
    out.extend_from_slice(&miniz_oxide::deflate::compress_to_vec(&raw, 6));
    Ok(out)
}

/// The clock (Hz) the fresh boot a state was saved against ran at: boot the one for `restore` at it.
pub fn clock_hz(state: &[u8]) -> Option<u64> {
    if state.len() < HEADER || &state[..8] != MAGIC {
        return None;
    }
    Some(u64::from_le_bytes(state[20..28].try_into().unwrap()))
}

/// The machine a state was saved from, given the fresh boot it was saved against again.
pub fn restore(state: &[u8], mut fresh: Soc) -> Result<Machine, String> {
    if state.len() < HEADER || &state[..8] != MAGIC {
        return Err("not a machine state".into());
    }
    if u32::from_le_bytes(state[8..12].try_into().unwrap()) != FORMAT {
        return Err("a machine state from another version".into());
    }
    let then = memories(&fresh);
    if u64::from_le_bytes(state[12..20].try_into().unwrap()) != fingerprint(&then) {
        return Err("a machine state of another firmware or flash".into());
    }
    let raw = miniz_oxide::inflate::decompress_to_vec_with_limit(&state[HEADER..], 64 << 20)
        .map_err(|_| "the machine state is damaged")?;

    let mut at = 0;
    let n = get_u32(&raw, &mut at)?;
    let rest: RestIn = bincode::deserialize(raw.get(at..at + n).ok_or("the state is cut short")?)
        .map_err(|_| "a machine state from another version")?;
    at += n;
    let mut mems: Vec<Vec<u8>> = Vec::with_capacity(then.len());
    for b in &then {
        let n = get_u32(&raw, &mut at)?;
        let a = raw.get(at..at + n).ok_or("the state is cut short")?;
        at += n;
        mems.push(a.iter().enumerate().map(|(i, &x)| x ^ b.get(i).copied().unwrap_or(0)).collect());
    }
    drop(then);

    let mut soc = rest.soc;
    let [ram, ovl, sram2, xip, flash, lcd] = <[Vec<u8>; 6]>::try_from(mems).unwrap();
    soc.ram = Ram::from_bytes(ram);
    soc.ovl = Ram::from_bytes(ovl);
    soc.sram2 = Ram::from_bytes(sram2);
    soc.xip = xip;
    soc.flash.mem = flash;
    soc.lcd.fb = lcd.chunks_exact(2).map(|p| u16::from_le_bytes([p[0], p[1]])).collect();
    // what a state leaves out, the debugging aids, as this process has them
    soc.audio_trace = fresh.audio_trace;
    soc.gpio_writes = fresh.gpio_writes.take();
    soc.flaky_core1 = fresh.flaky_core1.take();
    soc.flash.debug_from(&fresh.flash);
    soc.usb.trace = fresh.usb.trace;

    Ok(Machine {
        cpu: Cpu::from_core_state(&rest.cpu),
        cpu1: rest.cpu1.as_ref().map(Cpu::from_core_state),
        soc,
        pending_tick: rest.pending_tick,
    })
}

/// serde for arrays longer than its own (32)
pub(crate) mod arr {
    use serde::{Deserialize, Deserializer, Serialize, Serializer};

    pub fn serialize<S: Serializer, T: Serialize, const N: usize>(a: &[T; N], s: S) -> Result<S::Ok, S::Error> {
        a.as_slice().serialize(s)
    }

    pub fn deserialize<'de, D: Deserializer<'de>, T: Deserialize<'de>, const N: usize>(d: D) -> Result<[T; N], D::Error> {
        let v = Vec::<T>::deserialize(d)?;
        v.try_into().map_err(|v: Vec<T>| serde::de::Error::invalid_length(v.len(), &"the array's length"))
    }
}

pub(crate) mod boxed_arr {
    use serde::{Deserialize, Deserializer, Serialize, Serializer};

    pub fn serialize<S: Serializer, T: Serialize, const N: usize>(a: &[T; N], s: S) -> Result<S::Ok, S::Error> {
        super::arr::serialize(a, s)
    }

    pub fn deserialize<'de, D: Deserializer<'de>, T: Deserialize<'de>, const N: usize>(d: D) -> Result<Box<[T; N]>, D::Error> {
        super::arr::deserialize(d).map(Box::new)
    }
}

/// A register map in address order (a state of the same machine is the same bytes).
pub(crate) fn sorted_map<S: serde::Serializer, H>(m: &std::collections::HashMap<u32, u32, H>, s: S) -> Result<S::Ok, S::Error> {
    let mut v: Vec<(u32, u32)> = m.iter().map(|(&k, &v)| (k, v)).collect();
    v.sort_unstable();
    s.collect_map(v)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Action, OVL_BASE, RAM_BASE};
    use fm1_core::Bus;

    fn fresh() -> Soc {
        Soc::new(1 << 16, vec![0xaa; 0x1000])
    }

    #[test]
    fn a_machine_comes_back_as_it_was() {
        let mut soc = fresh();
        soc.write32(RAM_BASE + 0x100, 0x1234_5678);
        soc.write32(OVL_BASE + 8, 7);
        soc.write32(0x1eef100, 0x30);
        soc.periph.apply(&Action::Key { id: 20, down: true });
        soc.periph.apply(&Action::Midi { b: 0x90 });
        soc.lcd.fb[5] = 0xf800;
        soc.flash.mem[0x5000] = 0;
        let mut cpu = Cpu::new(0x0200_0000);
        cpu.regs[3] = 99;
        cpu.pred_skips.push((0x0200_0010, 0x0200_0020));
        let mut cpu1 = Cpu::new(0x01c0_22b6);
        cpu1.core_id = 1;

        let state = save(&cpu, Some(&cpu1), &soc, true, &fresh()).unwrap();
        assert!(state.len() < 16 << 10, "the zeros of the diff are deflated ({} bytes)", state.len());
        let mut m = restore(&state, fresh()).unwrap();
        assert_eq!(m.cpu.core_state(), cpu.core_state());
        assert_eq!(m.cpu1.as_ref().map(Cpu::core_state), Some(cpu1.core_state()));
        assert!(m.pending_tick);
        assert_eq!(m.soc.read32(RAM_BASE + 0x100), 0x1234_5678);
        assert_eq!((m.soc.lcd.fb[5], m.soc.flash.mem[0x5000]), (0xf800, 0));
        assert_eq!(save(&m.cpu, m.cpu1.as_ref(), &m.soc, true, &fresh()).unwrap(), state, "and saves as the same bytes");
        assert_eq!(clock_hz(&state), Some(fresh().periph.hz));
    }

    #[test]
    fn a_state_against_another_boot_is_refused() {
        let state = save(&Cpu::new(0), None, &fresh(), false, &fresh()).unwrap();
        let mut other = fresh();
        other.xip[0] = 0;
        assert!(restore(&state, other).is_err());
        assert!(restore(&state[..40], fresh()).is_err());
        assert!(restore(b"not a state at all, not even close", fresh()).is_err());
    }
}
