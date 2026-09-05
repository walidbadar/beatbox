/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdbool.h>

#include "ui.h"
#include "sequencer.h"

static ui_callbacks_t cbs;
static lv_obj_t *step_btn[NUM_TRACKS][NUM_STEPS];
static lv_obj_t *bpm_label;
static lv_obj_t *status_label;
static int current_bpm;

static const char *track_names[NUM_TRACKS] = { "Kick", "Snr", "Hat", "Tone" };

/* Relative weights for lv_obj_set_flex_grow() on the top-level rows.
 * The step grid (4 track rows) gets more of the screen than the bpm
 * or transport rows -- tune these, not pixel heights, to change the
 * proportions.
 */
#define GROW_BPM_ROW       1
#define GROW_TRACK_ROW     2
#define GROW_TRANSPORT_ROW 1

/* Explicit styling for each step button's off/on look. Plain lv_obj
 * default styling would already show a box, but this makes "on"
 * unmistakable rather than relying on the theme's default checked
 * look (which can be subtle).
 */
static lv_style_t style_step_off;
static lv_style_t style_step_on;
static bool step_styles_ready;

static void ensure_step_styles(void)
{
	if (step_styles_ready) {
		return;
	}

	lv_style_init(&style_step_off);
	lv_style_set_radius(&style_step_off, 3);
	lv_style_set_border_width(&style_step_off, 2);
	lv_style_set_border_color(&style_step_off, lv_palette_main(LV_PALETTE_GREY));
	lv_style_set_bg_color(&style_step_off, lv_color_white());
	lv_style_set_bg_opa(&style_step_off, LV_OPA_COVER);

	lv_style_init(&style_step_on);
	lv_style_set_bg_color(&style_step_on, lv_palette_main(LV_PALETTE_BLUE));
	lv_style_set_bg_opa(&style_step_on, LV_OPA_COVER);
	lv_style_set_border_color(&style_step_on, lv_palette_darken(LV_PALETTE_BLUE, 2));

	step_styles_ready = true;
}

/* Track/step are packed into one user_data pointer so every step
 * button can share this one callback instead of needing 64 separate
 * closures.
 */
static void step_btn_event_cb(lv_event_t *e)
{
	int packed = (int)(intptr_t)lv_event_get_user_data(e);
	int track = packed / 100;
	int step = packed % 100;

	/* the button already flipped its own LV_STATE_CHECKED visually
	 * (LV_OBJ_FLAG_CHECKABLE handles that) -- just tell the model
	 */
	if (cbs.toggle_step) {
		cbs.toggle_step(track, step);
	}
}

static void bpm_delta_cb(lv_event_t *e)
{
	int delta = (int)(intptr_t)lv_event_get_user_data(e);

	if (cbs.set_bpm) {
		cbs.set_bpm(current_bpm + delta);
	}
}

static void demo_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (cbs.load_demo) {
		cbs.load_demo();
	}
}

static void clear_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (cbs.clear) {
		cbs.clear();
	}
}

static void render_cb(lv_event_t *e)
{
	uint32_t bars = (uint32_t)(intptr_t)lv_event_get_user_data(e);

	if (cbs.render) {
		cbs.render(bars);
	}
}

static lv_obj_t *make_text_button(lv_obj_t *parent, const char *text,
				   lv_event_cb_t cb, void *user_data)
{
	lv_obj_t *btn = lv_btn_create(parent);

	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
	lv_obj_t *lbl = lv_label_create(btn);

	lv_label_set_text(lbl, text);
	lv_obj_center(lbl);
	return btn;
}

/* A full-width row whose height is a share of the screen (via flex
 * grow on the parent column), rather than a fixed pixel height.
 */
static lv_obj_t *make_row(lv_obj_t *parent, int flex_grow)
{
	lv_obj_t *row = lv_obj_create(parent);

	lv_obj_set_width(row, LV_PCT(100));
	lv_obj_set_height(row, LV_SIZE_CONTENT); /* flex_grow overrides this */
	lv_obj_set_flex_grow(row, flex_grow);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
			      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	return row;
}

void ui_init(const ui_callbacks_t *callbacks, const sequencer_t *seq)
{
	cbs = *callbacks;
	current_bpm = seq->bpm;
	ensure_step_styles();

	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		printk("UI: display device not ready -- check the devicetree "
		       "chosen zephyr,display node\n");
		return;
	}

	lv_obj_t *scr = lv_scr_act();

	/* scr is already exactly the display's resolution, so a column
	 * flex layout on it with flex_grow rows below is what makes the
	 * whole UI scale with screen size instead of any fixed pixel math.
	 */
	lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(scr, 2, 0);
	lv_obj_set_style_pad_all(scr, 4, 0);

	/* --- bpm row --- */
	lv_obj_t *bpm_row = make_row(scr, GROW_BPM_ROW);

	make_text_button(bpm_row, "-5", bpm_delta_cb, (void *)(intptr_t)(-5));
	bpm_label = lv_label_create(bpm_row);
	lv_label_set_text_fmt(bpm_label, "%d BPM", current_bpm);
	make_text_button(bpm_row, "+5", bpm_delta_cb, (void *)(intptr_t)(5));

	/* --- 4x16 step grid, one row of plain checkable buttons per track --- */
	for (int t = 0; t < NUM_TRACKS; t++) {
		lv_obj_t *row = make_row(scr, GROW_TRACK_ROW);

		/* packed edge-to-edge, not spread out like the button rows */
		lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
				      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_column(row, 3, 0);

		lv_obj_t *name_lbl = lv_label_create(row);

		/* content-sized, not a fixed percentage: the label's width
		 * adjusts to whatever text is passed to lv_label_set_text()
		 * here, so track_names[] entries can change length (or this
		 * can be swapped for a runtime-editable track name later)
		 * without leaving dead space or clipping the text.
		 */
		lv_label_set_text(name_lbl, track_names[t]);
		lv_obj_set_width(name_lbl, LV_SIZE_CONTENT);
		lv_obj_set_height(name_lbl, LV_PCT(100));

		for (int s = 0; s < NUM_STEPS; s++) {
			lv_obj_t *btn = lv_btn_create(row);

			lv_obj_set_height(btn, LV_PCT(100));
			lv_obj_set_flex_grow(btn, 1); /* 16 buttons split what's left */
			lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
			lv_obj_add_style(btn, &style_step_off, LV_STATE_DEFAULT);
			lv_obj_add_style(btn, &style_step_on, LV_STATE_CHECKED);

			int packed = t * 100 + s;

			lv_obj_add_event_cb(btn, step_btn_event_cb, LV_EVENT_CLICKED,
					    (void *)(intptr_t)packed);
			step_btn[t][s] = btn;
		}
	}

	/* --- transport row --- */
	lv_obj_t *transport_row = make_row(scr, GROW_TRANSPORT_ROW);

	make_text_button(transport_row, "Demo", demo_cb, NULL);
	make_text_button(transport_row, "Clear", clear_cb, NULL);
	make_text_button(transport_row, "Render x1", render_cb, (void *)(intptr_t)1);
	make_text_button(transport_row, "Render x2", render_cb, (void *)(intptr_t)2);

	/* --- status line -- content-height, not part of the grow split --- */
	status_label = lv_label_create(scr);
	lv_label_set_text(status_label, "Ready");

	display_blanking_off(display_dev);
	ui_sync_from_sequencer(seq);
}

void ui_sync_from_sequencer(const sequencer_t *seq)
{
	current_bpm = seq->bpm;
	if (bpm_label) {
		lv_label_set_text_fmt(bpm_label, "%d BPM", current_bpm);
	}

	for (int t = 0; t < NUM_TRACKS; t++) {
		for (int s = 0; s < NUM_STEPS; s++) {
			if (!step_btn[t][s]) {
				continue;
			}
			if (seq->steps[t][s]) {
				lv_obj_add_state(step_btn[t][s], LV_STATE_CHECKED);
			} else {
				lv_obj_clear_state(step_btn[t][s], LV_STATE_CHECKED);
			}
		}
	}
}

void ui_set_status(const char *text)
{
	if (status_label) {
		lv_label_set_text(status_label, text);
	}
}
