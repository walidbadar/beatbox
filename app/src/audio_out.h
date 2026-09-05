/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef AUDIO_OUT_H_
#define AUDIO_OUT_H_

#include <stdint.h>
#include <stdbool.h>
#include "sequencer.h"

/* Everything native_sim-specific about getting audio out of this app
 * lives behind these two calls. On real hardware, this .h stays the
 * same and only audio_out.c's insides change (point uart1's
 * devicetree node at a real UART pin pair, or swap the body for a
 * PWM/I2S/DAC driver call) -- main.c, sequencer.c and synth.c don't
 * need to know which one is happening.
 */

/* True once the underlying device (uart1 on native_sim) is ready.
 * Check this at boot to warn early instead of only failing at the
 * first render.
 */
bool audio_out_is_ready(void);

/* Renders `bars` bars of `seq` and streams the PCM out. Returns 0 on
 * success, -ENODEV if the output device isn't ready.
 */
int audio_out_render_and_stream(sequencer_t *seq, uint32_t bars);

#endif /* AUDIO_OUT_H_ */
