/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "synth.h"

/* --- helpers -------------------------------------------------------- */

static inline uint32_t phase_inc(uint32_t freq_hz)
{
	/* Q32 fixed point phase increment for a given frequency */
	return (uint32_t)(((uint64_t)freq_hz << 32) / SAMPLE_RATE);
}

static inline int16_t square_from_phase(uint32_t phase, int16_t amp)
{
	return (phase & 0x80000000u) ? -amp : amp;
}

/* 16-bit Galois LFSR, taps chosen for maximal length. Cheap, no
 * multiply/divide -- fine for a Cortex-M0 with no hardware RNG.
 */
static inline uint32_t lfsr_step(uint32_t *state)
{
	uint32_t lsb = *state & 1u;

	*state >>= 1;
	if (lsb) {
		*state ^= 0xB400u;
	}
	return *state;
}

static inline int16_t noise_sample(uint32_t *lfsr, int16_t amp)
{
	return (lfsr_step(lfsr) & 1u) ? amp : (int16_t)(-amp);
}

/* --- public API ------------------------------------------------------ */

void synth_voice_init(synth_voice_t *voice, sound_id_t sound)
{
	voice->sound = sound;
	voice->active = false;
	voice->age_samples = 0;
	voice->phase = 0;
	voice->lfsr = 0xACE1u; /* any nonzero seed */
}

void synth_voice_trigger(synth_voice_t *voice)
{
	voice->active = true;
	voice->age_samples = 0;
	voice->phase = 0;
	/* Deliberately don't reset the LFSR -- keeps successive hits
	 * from sounding identical, which a reset seed would cause.
	 */
}

static void render_kick(synth_voice_t *v, int32_t *out, size_t frames)
{
	const uint32_t decay = (SAMPLE_RATE * 220u) / 1000u;
	const uint32_t f_start = 180, f_end = 45;
	const int16_t max_amp = 12000;

	for (size_t i = 0; i < frames && v->active; i++) {
		if (v->age_samples >= decay) {
			v->active = false;
			break;
		}
		uint32_t freq = f_start - ((f_start - f_end) * v->age_samples) / decay;
		v->phase += phase_inc(freq);
		int32_t amp = max_amp - (int32_t)((max_amp * (uint64_t)v->age_samples) / decay);

		out[i] += square_from_phase(v->phase, (int16_t)amp);
		v->age_samples++;
	}
}

static void render_snare(synth_voice_t *v, int32_t *out, size_t frames)
{
	const uint32_t decay = (SAMPLE_RATE * 150u) / 1000u;
	const uint32_t tone_decay = (SAMPLE_RATE * 90u) / 1000u;
	const int16_t noise_amp = 9000;
	const int16_t tone_amp = 4000;

	for (size_t i = 0; i < frames && v->active; i++) {
		if (v->age_samples >= decay) {
			v->active = false;
			break;
		}
		int32_t namp = noise_amp - (int32_t)((noise_amp * (uint64_t)v->age_samples) / decay);
		int16_t sample = noise_sample(&v->lfsr, (int16_t)namp);

		if (v->age_samples < tone_decay) {
			v->phase += phase_inc(190);
			int32_t tamp = tone_amp -
				       (int32_t)((tone_amp * (uint64_t)v->age_samples) / tone_decay);
			sample += square_from_phase(v->phase, (int16_t)tamp);
		}
		out[i] += sample;
		v->age_samples++;
	}
}

static void render_hihat(synth_voice_t *v, int32_t *out, size_t frames)
{
	const uint32_t decay = (SAMPLE_RATE * 45u) / 1000u;
	const int16_t max_amp = 7000;

	for (size_t i = 0; i < frames && v->active; i++) {
		if (v->age_samples >= decay) {
			v->active = false;
			break;
		}
		int32_t amp = max_amp - (int32_t)((max_amp * (uint64_t)v->age_samples) / decay);

		/* step the LFSR twice per sample for a "brighter"/denser
		 * feel than the snare's noise, without needing a real
		 * high-pass filter
		 */
		lfsr_step(&v->lfsr);
		out[i] += noise_sample(&v->lfsr, (int16_t)amp);
		v->age_samples++;
	}
}

static void render_tone(synth_voice_t *v, int32_t *out, size_t frames)
{
	const uint32_t decay = (SAMPLE_RATE * 180u) / 1000u;
	const int16_t max_amp = 10000;

	for (size_t i = 0; i < frames && v->active; i++) {
		if (v->age_samples >= decay) {
			v->active = false;
			break;
		}
		v->phase += phase_inc(440);
		int32_t amp = max_amp - (int32_t)((max_amp * (uint64_t)v->age_samples) / decay);

		out[i] += square_from_phase(v->phase, (int16_t)amp);
		v->age_samples++;
	}
}

void synth_voice_render(synth_voice_t *voice, int32_t *out, size_t frames)
{
	if (!voice->active) {
		return;
	}

	switch (voice->sound) {
	case SOUND_KICK:
		render_kick(voice, out, frames);
		break;
	case SOUND_SNARE:
		render_snare(voice, out, frames);
		break;
	case SOUND_HIHAT:
		render_hihat(voice, out, frames);
		break;
	case SOUND_TONE:
		render_tone(voice, out, frames);
		break;
	default:
		break;
	}
}
