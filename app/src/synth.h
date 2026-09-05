/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNTH_H_
#define SYNTH_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SAMPLE_RATE 22050

typedef enum {
	SOUND_KICK = 0,
	SOUND_SNARE,
	SOUND_HIHAT,
	SOUND_TONE,
	SOUND_COUNT
} sound_id_t;

/* One synth voice: one per track. Very deliberately not a "real"
 * synth (no filters, no proper oscillator table) -- this is the
 * minimum needed to get four distinguishable percussive/tonal
 * sounds out of pure integer math, so it stays portable to a
 * Cortex-M with no FPU.
 */
typedef struct {
	sound_id_t sound;
	bool active;
	uint32_t age_samples;   /* samples since trigger */
	uint32_t phase;         /* Q32 phase accumulator for tonal voices */
	uint32_t lfsr;          /* noise state for noisy voices */
} synth_voice_t;

void synth_voice_init(synth_voice_t *voice, sound_id_t sound);
void synth_voice_trigger(synth_voice_t *voice);

/* Render `frames` samples for this voice and ADD them into out[]
 * (out is not cleared here -- caller is responsible for mixing).
 * out[] is int32_t so multiple voices can be summed without
 * wrapping before the caller clips down to int16_t.
 * Marks voice->active = false once its envelope has finished.
 */
void synth_voice_render(synth_voice_t *voice, int32_t *out, size_t frames);

#endif /* SYNTH_H_ */
