/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <lvgl.h>

#include "sequencer.h"
#include "ui.h"
#include "audio_out.h"

static sequencer_t seq;

/* --- LVGL UI callbacks --------------------------------------------------
 * The UI never mutates `seq` itself (see ui.h) -- it calls these, exactly
 * the way the shell commands below do, so the touchscreen and the shell
 * are two front ends onto one model rather than two copies of the logic.
 */

static void ui_cb_toggle_step(int track, int step)
{
	sequencer_toggle(&seq, track, step);
}

static void ui_cb_set_bpm(int bpm)
{
	sequencer_set_bpm(&seq, (uint16_t)bpm);
	ui_sync_from_sequencer(&seq);
}

static void ui_cb_load_demo(void)
{
	sequencer_load_demo_pattern(&seq);
	ui_sync_from_sequencer(&seq);
	ui_set_status("Demo pattern loaded");
}

static void ui_cb_clear(void)
{
	memset(seq.steps, 0, sizeof(seq.steps));
	ui_sync_from_sequencer(&seq);
	ui_set_status("Pattern cleared");
}

static void ui_cb_render(uint32_t bars)
{
	ui_set_status("Streaming...");
	int ret = audio_out_render_and_stream(&seq, bars);

	ui_set_status(ret == 0 ? "Done" : "uart1 not ready");
}

/* --- shell commands (all on uart0, unaffected by the above) ----------- */

static int cmd_seq_bpm(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "usage: seq bpm <40-300>");
		return -EINVAL;
	}
	sequencer_set_bpm(&seq, (uint16_t)atoi(argv[1]));
	ui_sync_from_sequencer(&seq);
	shell_print(sh, "bpm set to %d", seq.bpm);
	return 0;
}

static int cmd_seq_toggle(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "usage: seq toggle <track 0-3> <step 0-15>");
		return -EINVAL;
	}
	int track = atoi(argv[1]);
	int step = atoi(argv[2]);

	sequencer_toggle(&seq, track, step);
	ui_sync_from_sequencer(&seq);
	shell_print(sh, "track %d step %d -> %s", track, step,
		    seq.steps[track][step] ? "on" : "off");
	return 0;
}

static int cmd_seq_pattern_demo(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	sequencer_load_demo_pattern(&seq);
	ui_sync_from_sequencer(&seq);
	shell_print(sh, "loaded demo pattern (kick/snare/hihat/tone), bpm=%d", seq.bpm);
	return 0;
}

static int cmd_seq_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	memset(seq.steps, 0, sizeof(seq.steps));
	ui_sync_from_sequencer(&seq);
	shell_print(sh, "pattern cleared");
	return 0;
}

static int cmd_seq_render(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t bars = 1;

	if (argc == 2) {
		bars = (uint32_t)atoi(argv[1]);
	}
	if (bars == 0 || bars > 8) {
		shell_error(sh, "usage: seq render [bars 1-8]");
		return -EINVAL;
	}
	shell_print(sh, "streaming %d bar(s) at %d bpm out uart1...", bars, seq.bpm);
	int ret = audio_out_render_and_stream(&seq, bars);

	if (ret == 0) {
		shell_print(sh, "done.");
	}
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_seq,
	SHELL_CMD_ARG(bpm, NULL, "set tempo: seq bpm <40-300>", cmd_seq_bpm, 2, 0),
	SHELL_CMD_ARG(toggle, NULL, "toggle a step: seq toggle <track> <step>",
		      cmd_seq_toggle, 3, 0),
	SHELL_CMD(pattern_demo, NULL, "load the built-in demo pattern", cmd_seq_pattern_demo),
	SHELL_CMD(clear, NULL, "clear all steps", cmd_seq_clear),
	SHELL_CMD_ARG(render, NULL, "stream N bars of PCM out uart1: seq render [bars]",
		      cmd_seq_render, 1, 1),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(seq, &sub_seq, "Pocket Operator style step sequencer", NULL);

int main(void)
{
	sequencer_init(&seq);
	sequencer_load_demo_pattern(&seq);

	printk("\nbeatbox ready. Demo pattern loaded, bpm=%d.\n", seq.bpm);
	printk("Audio streams out uart1 as raw PCM (see README for how to capture it).\n");
	printk("Try: seq render 2 / seq toggle 3 2 / seq bpm 140, or use the touchscreen.\n\n");

	if (!audio_out_is_ready()) {
		printk("WARNING: audio output device not ready at boot -- check the "
		       "devicetree overlay before expecting audio out.\n");
	}

	ui_callbacks_t ui_cbs = {
		.toggle_step = ui_cb_toggle_step,
		.set_bpm = ui_cb_set_bpm,
		.load_demo = ui_cb_load_demo,
		.clear = ui_cb_clear,
		.render = ui_cb_render,
	};
	ui_init(&ui_cbs, &seq);

	/* auto-stream once at boot so you get audio even without touching
	 * the screen or the shell.
	 */
	audio_out_render_and_stream(&seq, 2);

	/* LVGL needs periodic pumping to redraw and to poll the SDL mouse
	 * (acting as a touch input) on native_sim. The shell and audio
	 * streaming both run fine from their own threads regardless of
	 * this loop -- this is purely the display's heartbeat.
	 */
	while (1) {
		lv_timer_handler();
		k_msleep(10);
	}

	return 0;
}
