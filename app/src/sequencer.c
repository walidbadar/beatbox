/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "sequencer.h"

/* Chunk size for rendering each step. Kept small and fixed so RAM
 * usage doesn't depend on BPM or how many bars are requested --
 * this is the piece that would stay identical when this code moves
 * off native_sim onto a real MCU driving a DAC/PWM/I2S peripheral.
 */
#define STEP_BUF_MAX 512

static const sound_id_t track_sound[NUM_TRACKS] = {
	SOUND_KICK,
	SOUND_SNARE,
	SOUND_HIHAT,
	SOUND_TONE,
};

void sequencer_init(sequencer_t *seq)
{
	memset(seq->steps, 0, sizeof(seq->steps));
	seq->bpm = 120;
	for (int t = 0; t < NUM_TRACKS; t++) {
		synth_voice_init(&seq->voices[t], track_sound[t]);
	}
}

void sequencer_toggle(sequencer_t *seq, int track, int step)
{
	if (track < 0 || track >= NUM_TRACKS || step < 0 || step >= NUM_STEPS) {
		return;
	}
	seq->steps[track][step] = !seq->steps[track][step];
}

void sequencer_set_bpm(sequencer_t *seq, uint16_t bpm)
{
	if (bpm < 40) {
		bpm = 40;
	} else if (bpm > 300) {
		bpm = 300;
	}
	seq->bpm = bpm;
}

void sequencer_load_demo_pattern(sequencer_t *seq)
{
	memset(seq->steps, 0, sizeof(seq->steps));

	/* kick on the four-on-the-floor beats */
	seq->steps[0][0] = true;
	seq->steps[0][4] = true;
	seq->steps[0][8] = true;
	seq->steps[0][12] = true;

	/* snare on the backbeat */
	seq->steps[1][4] = true;
	seq->steps[1][12] = true;

	/* hihat every other step */
	for (int s = 0; s < NUM_STEPS; s += 2) {
		seq->steps[2][s] = true;
	}

	/* a little melodic tone accent */
	seq->steps[3][0] = true;
	seq->steps[3][7] = true;
	seq->steps[3][10] = true;

	seq->bpm = 120;
}

void sequencer_render(sequencer_t *seq, uint32_t bars, sequencer_emit_cb_t emit)
{
	uint32_t step_samples = (SAMPLE_RATE * 60u) / ((uint32_t)seq->bpm * 4u);
	int32_t mix[STEP_BUF_MAX];
	int16_t buf[STEP_BUF_MAX];

	for (uint32_t bar = 0; bar < bars; bar++) {
		for (int step = 0; step < NUM_STEPS; step++) {
			for (int t = 0; t < NUM_TRACKS; t++) {
				if (seq->steps[t][step]) {
					synth_voice_trigger(&seq->voices[t]);
				}
			}

			uint32_t remaining = step_samples;

			while (remaining > 0) {
				uint32_t chunk = remaining > STEP_BUF_MAX ? STEP_BUF_MAX : remaining;

				memset(mix, 0, chunk * sizeof(int32_t));
				for (int t = 0; t < NUM_TRACKS; t++) {
					synth_voice_render(&seq->voices[t], mix, chunk);
				}
				/* soft clip in 32-bit, then narrow to int16_t --
				 * cheaper than a real limiter, good enough for
				 * four voices max
				 */
				for (uint32_t i = 0; i < chunk; i++) {
					int32_t s = mix[i];

					if (s > 16000) {
						s = 16000;
					} else if (s < -16000) {
						s = -16000;
					}
					buf[i] = (int16_t)s;
				}

				if (emit) {
					emit(buf, chunk);
				}
				remaining -= chunk;
			}
		}
	}
}
