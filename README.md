# FM-1Ception

Up to eight emulated **M-VAVE FM-1** pocket synths in one instrument plugin (AU, VST3 and a
standalone app), each running its own firmware on its own CPU core.

![Four engines side by side, one waiting for a firmware, one with its panel buttons up](fm1ception/docs/media/four-engines.png)

The FM-1 has a lively community writing its own firmwares: grooveboxes, trackers, drum
machines, chord harps, generative ambient machines and more. FM-1Ception runs them unchanged,
thanks to Joakim Eriksson's [fm1-emu](https://github.com/joakimeriksson/fm1-emu), which emulates
the FM-1's CPU, screen, keys, knobs and audio. This project wraps that emulator in a JUCE plugin
built for playing several firmwares at once:

- **Up to eight engines** in one instance. Each engine is an independent FM-1 on a thread of its
  own, so they run in parallel on separate cores, and their outputs are summed.
- **The screen comes first.** It is drawn at twice the real FM-1's size, and the controls are
  compact underneath: the four knobs, then MASTER / SELECT / PRESETS / ALGO / OCT-/+ on the left,
  a mini keyboard on the right.
- **Tiled or tabbed.** Tiled lays every engine out in a grid that reflows as you resize the
  window (4 across or 2×2, 8 across or 4×2). Tabbed shows one at a time under browser-style tabs.
- **Soft knobs.** Many firmwares draw their own knobs on screen. Tell FM-1Ception where they are
  and you can turn them by dragging on the screen.
- **MIDI routing** for several engines: play the selected engine, or one engine per MIDI channel.
- **Level meters** for each engine, plus a main output meter whose caret sets the overall volume.

It runs on macOS (Apple silicon and Intel), 64-bit Windows and 64-bit Linux (VST3 and standalone;
Linux is built from source, see [Building](#building)).

This repository is a fork of [fm1-emu](https://github.com/joakimeriksson/fm1-emu): the emulator
is at its top level as upstream has it, with the changes FM-1Ception needs, and the plugin is in
[`fm1ception/`](fm1ception). For the emulator itself (its own desktop, browser and plugin
builds), see [upstream's README](https://github.com/joakimeriksson/fm1-emu#readme).

## Getting firmware

FM-1Ception ships with no firmware. Firmware packages are `.fwsc` files, the same update packages
you would install on a real FM-1:

- **Community firmwares:** [the FM-1 Firmware Hub](https://thefirmwarehub.com/) and
  [fm1-editor.com](https://fm1-editor.com/firmware/) link each author's releases. fm1-emu's
  [README](https://github.com/joakimeriksson/fm1-emu#what-runs) lists the 80-plus that run in the
  emulator, and you can try any of them in your browser at
  [joakimeriksson.github.io/fm1-emu](https://joakimeriksson.github.io/fm1-emu/).
- **M-VAVE's stock firmware:** download `FM-1.fwsc` from M-VAVE. It is much heavier to emulate
  than the community firmwares: on an Apple silicon Mac it runs at about half of real time while
  a note sounds, so expect dropouts. It also listens on MIDI channel 2 (see [MIDI](#midi)).

## Using it

### Loading a firmware

Click **OPEN BROWSER** on an empty engine's screen to pick one from the Firmware Library (or
**LOAD FIRMWARE** under it for a file dialog), drop a `.fwsc` or `.fm1save` file onto the engine,
or use **MENU › Firmware Library** or **MENU › Load Firmware**. The firmware, and everything it has saved to its flash
(patches, settings, samples), is stored in your project, so the song reopens with the same sounds.

**MENU › Export Save** writes a `.fm1save`: the firmware plus its saved state in one file, which
also opens in fm1-emu's browser and plugin versions. Changes a firmware is only holding in memory
need its own Save command first.

**MENU › Init Engine** clears an engine back to empty.

### The header

- **MENU:** firmware, MIDI, CPU clock and layout settings. Each engine also has its own menu:
  click the ☰ at the right end of the strip above its screen, or right-click the engine.
- **The meter** in the middle is the plugin's output. Drag its caret to set the main volume. This
  is separate from each engine's MASTER knob, and the host can automate both.
- **ENGINES 1–8** sets how many engines run. An engine with no firmware loaded costs next to
  nothing.

### Each engine's panel

![One engine in the tabbed layout](fm1ception/docs/media/tabbed.png)

- Click the keys of the mini keyboard to play, and drag or scroll the knobs to turn them.
- **The panel buttons** (FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC) are
  hidden to keep things small. Hold **Shift** with the mouse over an engine to show them over its
  controls, or click the grid button at the bottom right to keep them up. Right-click a button to
  latch it down; right-click again to release it.
- **The small meter** beside MASTER is that engine's own output.
- The strip above the screen shows the firmware's name, its CPU clock and how much faster than
  real time it could run on this computer.

### Soft knobs

Many firmwares draw their own knobs on screen, usually a row of four controlled by KNOB1–4. To
turn them by touching the screen:

1. Right-click the screen at the height of those on-screen knobs, and choose
   **Set Knob Y Value Here**.
2. Now a band around that height works like the four knobs, one per quarter of the screen's
   width. Drag up or down, or scroll, to turn them. The four zones are outlined while the pointer
   is over them.
3. If you set it by mistake, right-click and choose **Remove Knobs Set Position**.

The position is remembered per firmware, for every engine and every project, in
`~/Library/Application Support/Xfer Records/FM-1Ception/Firmware/` on macOS
(`%APPDATA%\Xfer Records\FM-1Ception\Firmware\` on Windows, `~/.config/Xfer Records/FM-1Ception/Firmware/`
on Linux): one small JSON file per firmware.

### Layout

**MENU › Layout** switches between:

- **Tiled:** every engine at once, in whichever grid gives them the most room in the window.
- **Tabbed:** one engine at a time. Each tab shows the engine's number, firmware and output
  level. Click a tab to switch, or right-click it for that engine's menu.

Clicking an engine selects it. In Tiled the selected engine has an amber line under its strip.

### MIDI

**MENU › MIDI** picks how incoming MIDI reaches the engines:

| Mode | What plays |
|---|---|
| **Selected Engine** (default) | Every channel plays the selected engine, sent to its firmware on Ch 1. |
| **MultiChannel** | Ch 1 plays engine 1, Ch 2 plays engine 2, and so on, each sent to its firmware on Ch 1. |
| **Custom** | Each engine has its own receive channel (Omni or 1–16). |

Below the modes, each engine's submenu sets which channel its firmware hears. For example, set
**Send to Firmware On › Ch 2** for M-VAVE's stock firmware, which listens on channel 2. When the
routing changes, any notes still held on an engine that stops hearing them are released, so
nothing hangs.

### CPU clock

**MENU › CPU Clock** sets the emulated clock for one engine or all of them: 120, 160, **240**
(the default), 320 or 400 MHz. 240 MHz is the real chip's clock, and it is also the cheapest
setting for your computer: firmwares spend their spare time waiting, and at a higher clock the
emulator has more of that waiting to emulate. Only raise it if a firmware runs out of cycles.
Changing it restarts the firmware.

On an Apple silicon Mac, most community firmwares run at 4–12× real time per engine at 240 MHz
with a chord held, so eight engines fit comfortably. Firmware code is compiled to native code
(JIT) on Apple silicon, Windows and Linux; elsewhere it is interpreted, which is much slower.

The emulator runs a little ahead of the host and reports that as the plugin's latency, so the
host compensates and notes land where they were placed.

## Building

You need:

- **CMake** 3.22 or later, and **Ninja** (`brew install cmake ninja`)
- **Rust** (stable, via [rustup](https://rustup.rs)). CMake finds `cargo` on your `PATH` or in
  `~/.cargo/bin`.
- **[JUCE](https://github.com/juce-framework/JUCE)** 8 or later (developed against 9.0.2). By
  default the build looks for it beside the repository, at `../Libraries/Juce`; point elsewhere
  with `-DJUCE_DIR=/path/to/JUCE`. If it is in neither place, CMake downloads JUCE 9.0.2 into the
  build folder when it configures (23 MB, once).
- Xcode's command line tools on macOS, or Visual Studio 2026 with the C++ tools on Windows.
- On Linux, GCC or Clang and the libraries JUCE needs. On Debian or Ubuntu:

  ```sh
  sudo apt install build-essential pkg-config cmake ninja-build libasound2-dev libcurl4-openssl-dev \
      libfontconfig1-dev libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
      libxi-dev libxinerama-dev libxrandr-dev libxrender-dev
  ```

```sh
git clone https://github.com/stevexfer/FM-1Ception.git
cd FM-1Ception
cmake -S fm1ception -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

No submodules are needed for the plugin.

On macOS this builds universal binaries (Apple silicon and Intel). To build only for your own Mac,
which takes half the time, add `-DCMAKE_OSX_ARCHITECTURES=arm64`. The plugins are not installed
by the build: they are in `build/FM1Ception_artefacts/Release/` (AU, VST3, Standalone). On Linux,
copy `FM-1Ception.vst3` into `~/.vst3/` for your hosts to find it.

### Installers

`fm1ception/installer/build_all.sh` makes the release installers: a signed, notarized macOS
`.pkg`, and a Windows installer built in a Parallels VM with Inno Setup. Signing comes from the
environment or a git-ignored `fm1ception/installer/macos/signing.local.sh` (see
`make_installer.sh`).

### Tests

The build also makes a console smoke test that drives the real processor. It checks engines
starting and stopping, state round trips, firmware preferences and, given a firmware, that it
boots, sounds, routes MIDI correctly and keeps up in real time with four engines:

```sh
build/FM1CeptionTests_artefacts/Release/FM1CeptionTests path/to/firmware.fwsc
```

Without an argument it uses `$FM1C_TEST_FIRMWARE`, or looks in `~/Documents/FM1/`. With
`--snapshot DIR` it renders the editor to PNGs instead (that's where the pictures here come from).
Set `FM1C_TEST_NETWORK=1` to also fetch the real firmware lists over HTTPS.

## How it's put together

- **The top level** is fm1-emu, as upstream has it, with FM-1Ception's changes to the emulator
  (save states among them). Upstream's changes are merged in from its `master`.
- **`fm1ception/engine/`** is a small Rust crate that compiles fm1-emu's CPU and chip models,
  and its plugin's engine thread, straight out of `crates/`, and exposes them to C++ through a C
  API (`engine/include/fm1ception_engine.h`). Each engine runs on its own high-priority thread
  and talks to the audio thread through lock-free ring buffers.
- **`fm1ception/Source/`** is the JUCE plugin: the processor (engines, MIDI routing, mixing,
  state), the editor, and `EngineView`, which draws one FM-1.
- **`fm1ception/Tests/`** has the smoke test.

## License

FM-1Ception's own code is MIT licensed (see [fm1ception/LICENSE](fm1ception/LICENSE)), as is
fm1-emu (see [LICENSE](LICENSE)), which carries its own copyright notice.

The plugin is built with [JUCE](https://juce.com), which is dual licensed under AGPLv3 and a
commercial licence. If you distribute binaries you build from this code, you need to meet JUCE's
terms as well, either by releasing under the AGPLv3 or by holding a JUCE licence.

Firmwares belong to their authors and are not part of this repository. FM-1 is a product of
M-VAVE; this project isn't affiliated with or endorsed by M-VAVE.

## Credits

- [Joakim Eriksson](https://github.com/joakimeriksson) for
  [fm1-emu](https://github.com/joakimeriksson/fm1-emu), which does all the actual emulation, and
  the community reverse engineering it builds on (see its
  [credits](https://github.com/joakimeriksson/fm1-emu#credits)).
- Every firmware author who publishes their work.
