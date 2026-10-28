/*
    One emulated FM-1 per handle (engine/src/lib.rs), each on a thread of its own that runs a
    little ahead of the host: it keeps a ring of 44.1 kHz stereo frames `lead` frames ahead of
    what has been popped, and takes each MIDI message when it reaches the frame it is stamped with.

    Threads:
      - fm1e_push_midi and fm1e_pop belong to the audio thread: one thread at a time per handle.
      - fm1e_load_file and fm1e_export_file block until the engine answers: call them from a
        background thread, never the message or audio thread.
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

bool fm1e_push_midi (const Fm1e*, uint64_t frame, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t len);
bool fm1e_pop (const Fm1e*, float* lr);
void fm1e_starved (const Fm1e*, uint64_t frames);

#ifdef __cplusplus
}
#endif
