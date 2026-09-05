/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UI_H_
#define UI_H_

#include <stdint.h>
#include "sequencer.h"

/* The UI never touches the sequencer directly -- it only calls these,
 * the same way the shell commands in main.c do. Keeps the touchscreen
 * and the shell as two equally-valid front ends to one model, instead
 * of the UI reaching into sequencer state on its own.
 */
typedef struct {
	void (*toggle_step)(int track, int step);
	void (*set_bpm)(int bpm);
	void (*load_demo)(void);
	void (*clear)(void);
	void (*render)(uint32_t bars);
} ui_callbacks_t;

/* Builds the screen and turns the display on. Call once at boot,
 * after the sequencer itself is initialized.
 */
void ui_init(const ui_callbacks_t *cbs, const sequencer_t *seq);

/* Re-reads seq's bpm + step grid and redraws the on-screen state to
 * match. Call this after ANY mutation to the sequencer that didn't
 * come through a ui_callbacks_t call -- i.e. after every shell
 * command -- so the touchscreen and shell never show different
 * patterns.
 */
void ui_sync_from_sequencer(const sequencer_t *seq);

/* One-line status text at the bottom of the screen, e.g. "Streaming..." */
void ui_set_status(const char *text);

#endif /* UI_H_ */
