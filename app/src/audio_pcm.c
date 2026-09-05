/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The ONE native_sim-specific file in this app: since native_sim has
 * no DAC/PWM/I2S peripheral, rendered PCM is streamed out uart1 as
 * raw bytes instead -- on native_sim that shows up as a pseudo-tty
 * (see wav_capture.py), on real hardware you'd point uart1's
 * devicetree node at an actual UART pin pair going to a codec/DAC
 * module, or replace this file's insides with a PWM/I2S/DAC driver
 * call. Either way, sequencer.c / synth.c never touch a peripheral
 * directly, so they don't change.
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "audio_out.h"
#include "synth.h" /* for SAMPLE_RATE */

bool audio_out_is_ready(void)
{
	return 0;
}

int audio_out_render_and_stream(sequencer_t *seq, uint32_t bars)
{
	if (!audio_out_is_ready()) {
		printk("PCM device not ready\n");
		return -ENODEV;
	}

	printk("audio_out_render_and_stream not implemented\n");
	return -ENOTSUP;
}
