/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SEQUENCER_H_
#define SEQUENCER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "synth.h"

#define NUM_TRACKS 4
#define NUM_STEPS  16

/* Track -> sound mapping is fixed, like a Pocket Operator: each
 * track is one voice, not an arbitrary instrument slot.
 */
typedef struct {
	bool steps[NUM_TRACKS][NUM_STEPS];
	synth_voice_t voices[NUM_TRACKS];
	uint16_t bpm;
} sequencer_t;

void sequencer_init(sequencer_t *seq);
void sequencer_toggle(sequencer_t *seq, int track, int step);
void sequencer_set_bpm(sequencer_t *seq, uint16_t bpm);
void sequencer_load_demo_pattern(sequencer_t *seq);

/* Renders `bars` bars (16 steps each) of audio and calls emit() with
 * each step's worth of samples as they're produced, instead of
 * building one giant buffer -- this keeps RAM use flat regardless of
 * how many bars are requested, which matters once this runs on an
 * actual microcontroller instead of native_sim.
 */
typedef void (*sequencer_emit_cb_t)(const int16_t *frames, size_t count);

void sequencer_render(sequencer_t *seq, uint32_t bars, sequencer_emit_cb_t emit);

#endif /* SEQUENCER_H_ */
