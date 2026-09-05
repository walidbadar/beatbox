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

#define UART1_NODE DT_NODELABEL(uart1)

static const struct device *const uart1_dev = DEVICE_DT_GET(UART1_NODE);

/* Tiny 8-byte header so the host script can pull the sample rate out
 * of the stream instead of hardcoding it: 4-byte magic + little-endian
 * uint32 sample rate, then raw int16 LE PCM samples follow with no
 * further framing.
 */
static void uart1_send_header(uint32_t rate)
{
	static const char magic[4] = { 'P', 'S', 'I', 'M' };

	for (int i = 0; i < 4; i++) {
		uart_poll_out(uart1_dev, (uint8_t)magic[i]);
	}
	for (int i = 0; i < 4; i++) {
		uart_poll_out(uart1_dev, (uint8_t)((rate >> (8 * i)) & 0xFF));
	}
}

static void emit_uart1_chunk(const int16_t *frames, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		uint16_t s = (uint16_t)frames[i];

		uart_poll_out(uart1_dev, (uint8_t)(s & 0xFF));
		uart_poll_out(uart1_dev, (uint8_t)((s >> 8) & 0xFF));
	}
}

bool audio_out_is_ready(void)
{
	return device_is_ready(uart1_dev);
}

int audio_out_render_and_stream(sequencer_t *seq, uint32_t bars)
{
	if (!audio_out_is_ready()) {
		printk("uart1 not ready -- check the devicetree overlay "
		       "(boards/native_sim.overlay) and that its Kconfig "
		       "driver is enabled\n");
		return -ENODEV;
	}
	uart1_send_header(SAMPLE_RATE);
	sequencer_render(seq, bars, emit_uart1_chunk);
	return 0;
}
