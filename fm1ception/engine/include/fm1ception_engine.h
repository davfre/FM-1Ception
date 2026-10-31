/*
    One emulated FM-1 per handle (engine/src/lib.rs), each on a thread of its own that runs a
    little ahead of the host: it keeps a ring of 44.1 kHz stereo frames `lead` frames ahead of
    what has been popped, and takes each MIDI message when it reaches the frame it is stamped with.

    Threads:
      - fm1e_push_midi and fm1e_pop belong to the audio thread: one thread at a time per handle.
      - fm1e_load_file, fm1e_export_file and fm1e_capture_flash block until the engine answers: call them from a
        background thread, never the message or audio thread. fm1e_save_state blocks too, for a few ms (it is
        what a host's state save waits for).
      - everything else may be called from any thread.
*/
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Fm1e Fm1e;

typedef struct Fm1eStatus
{
    uint8_t booted;   /* a firmware runs */
    uint8_t halted;   /* it hit an instruction the emulator does not know */
    uint8_t smk37;    /* the package is for the SMK-37's board */
    uint8_t jit;      /* firmware code is compiled to native code (else interpreted) */
    float speed;      /* how much faster than real time the engine could run, over the last second */
    uint64_t late;    /* frames the host found missing in the last second */
} Fm1eStatus;

enum { FM1E_LCD_W = 240, FM1E_LCD_H = 240, FM1E_LEDS = 41, FM1E_RATE = 44100 };

Fm1e* fm1e_create (void);
void fm1e_destroy (Fm1e*);

bool fm1e_jit_available (void);
/* 240 MHz: the chip's clock, and the least host CPU an engine can cost */
uint32_t fm1e_default_mhz (void);

void fm1e_set_lead (const Fm1e*, uint32_t frames);
/* the emulated CPU clock; a change reboots the firmware, keeping its flash */
void fm1e_set_clock (const Fm1e*, uint32_t mhz);
/* the MASTER pot, 0..1 */
void fm1e_set_master (const Fm1e*, float value);

/* flash blobs: per written sector its offset (uint32 little-endian) then its 4096 bytes */
int fm1e_boot (const Fm1e*, const char* name, const uint8_t* pkg, size_t pkgLen, const uint8_t* flash, size_t flashLen);
void fm1e_unload (const Fm1e*);
int fm1e_load_file (const Fm1e*, const char* utf8Path, char* msg, size_t msgCap);
int fm1e_export_file (const Fm1e*, const char* utf8Path, char* msg, size_t msgCap);
/* bring fm1e_flash's copy (refreshed every two seconds) up to the live flash now; blocks */
int fm1e_capture_flash (const Fm1e*, char* msg, size_t msgCap);

/* machine states: the whole emulated machine (cores, RAM, peripherals, screen) as it is now, a
   diff against the firmware booted afresh with its flash (tens of KB). fm1e_save_state takes one
   for fm1e_state and brings fm1e_flash's copy to the flash it goes with; fm1e_boot_state boots
   a firmware and flash with it in place of the fresh machine (a plain boot if it does not fit),
   and fm1e_restore_result says how that went, once: 0 restored, -1 booted afresh (msg: why),
   1 nothing to report */
int fm1e_save_state (const Fm1e*, char* msg, size_t msgCap);
size_t fm1e_state (const Fm1e*, uint8_t* out, size_t cap);
int fm1e_boot_state (const Fm1e*, const char* name, const uint8_t* pkg, size_t pkgLen,
                     const uint8_t* flash, size_t flashLen, const uint8_t* state, size_t stateLen);
int fm1e_restore_result (const Fm1e*, char* msg, size_t msgCap);

/* copy out what fits, return the full length */
size_t fm1e_name (const Fm1e*, uint8_t* out, size_t cap);
size_t fm1e_package (const Fm1e*, uint8_t* out, size_t cap);
size_t fm1e_flash (const Fm1e*, uint8_t* out, size_t cap);

/* matrix ids: 0 OCT-, 1 OCT+, 2..13 FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC, 14..40 keys F3..G5 */
void fm1e_key (const Fm1e*, uint8_t id, bool down);
/* matrix encoders: 0 SELECT, 1 ALGORITHM, 2..5 KNOB1..4, 6 PRESETS */
void fm1e_enc (const Fm1e*, uint8_t enc, int32_t clicks);

/* lcd: 240 x 240 RGB565; leds: 41 lit fractions (either may be null) */
void fm1e_view (const Fm1e*, uint16_t* lcd, float* leds, Fm1eStatus* status);

/* the firmware library's look at a .fwsc's bytes, without booting it (any thread): its identity
   ("FM-1_901"), the name it gives itself ("SHRUB V0.4" from its code, "FM-1 Stock V15" for a stock
   release; empty if neither) and a digest of the bytes (32 hex digits) that knows the file after a
   move or rename. Each NUL-terminated, cut to fit. 0: an FM-1 package; -1: not one (name: why, digest
   still set) */
int fm1e_probe_package (const uint8_t* pkg, size_t len, char* identity, size_t identityCap,
                        char* name, size_t nameCap, char* digest, size_t digestCap);

/* A package booted fresh (its factory flash) with no engine, run flat out: its screen (FM1E_LCD_W x
   FM1E_LCD_H RGB565) when its own clock reaches each of the n times in atMs (ascending), into lcd one
   after another (n * 57600 values). Blocks for a second or so, at most about 30 s: call it from a
   background thread. Returns the screens taken (fewer than n if the firmware stopped after the
   first), or -1 (msg: why none; NUL-terminated, cut to fit) */
int fm1e_screenshots (const uint8_t* pkg, size_t len, const uint32_t* atMs, size_t n, uint16_t* lcd,
                      char* msg, size_t msgCap);

bool fm1e_push_midi (const Fm1e*, uint64_t frame, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t len);
bool fm1e_pop (const Fm1e*, float* lr);
void fm1e_starved (const Fm1e*, uint64_t frames);

#ifdef __cplusplus
}
#endif
