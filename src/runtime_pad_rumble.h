#ifndef DCP_RUNTIME_PAD_RUMBLE_H
#define DCP_RUNTIME_PAD_RUMBLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call after the menu changed the rumble setting: logs it and stops any rumble already running when it is off. */
void runtime_pad_rumble_changed(void);
/* Reset gamepad face/button remaps to defaults and apply a DCP_PAD_MAP spec live. */
void runtime_pad_set_map(const char *spec);
/* Poll DCP_PAD_FILE outside game reads (e.g. while an overlay menu holds game input neutral). */
void runtime_pad_pump_inject(void);

#ifdef __cplusplus
}
#endif
#endif
