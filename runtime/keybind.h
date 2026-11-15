/* Keyboard and mouse bindings (backend_win32.c; other backends have none). Each Switch input (a button, a stick
   direction, or the lasso key) takes up to BIND_SLOTS keys or mouse buttons, saved as bind_<input> in the ini's
   [Input] section. A code is a Windows virtual key (1-255) or BIND_MOUSE + 0..4 (left, right, middle, back, forward
   mouse button); 0 is an empty slot. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define BIND_SLOTS 2
#define BIND_MOUSE 256
#define BIND_KEY_BACK 0x08     /* while rebinding: Backspace and Delete clear the slot, Esc cancels */
#define BIND_KEY_ESCAPE 0x1B
#define BIND_KEY_DELETE 0x2E

int bind_count(void);                                   /* the bindable inputs */
const char* bind_input_key(int i);                      /* its ini key: "bind_a" */
const char* bind_input_label(int i);                    /* its name: "A", "Left stick up" */
int bind_input_bit(int i);                              /* 0-15 buttons, 40-47 stick directions, 48 the lasso key */
const char* bind_default(int i);                        /* its default ini value: "E,Enter" */
int bind_parse(const char* value, int codes[BIND_SLOTS]);              /* ini value -> codes, returns how many */
void bind_format(const int codes[BIND_SLOTS], char* out, int cap);     /* codes -> ini value */
const char* bind_code_name(int code);                   /* short display name: "E", "Space", "Mouse 1"; NULL for 0 */
const char* bind_refused(int code, int fast_forward);   /* why a key can't be bound (a hotkey), NULL when it can */
/* a window message that presses a key or mouse button: its code; 0 for other messages, -1 for a held key's repeat */
int bind_code_from_msg(unsigned msg, unsigned long long wparam, long long lparam);
/* the old mouse_buttons option (Switch buttons for the left, right, middle, back and forward mouse buttons): input i's
   value with the mouse buttons it gives that input instead of the ones it had. 0 when it is the old default (no change) */
int bind_mouse_buttons(int i, const char* mouse_buttons, const char* value, char* out, int cap);
void bind_mouse_buttons_apply(const char* mouse_buttons);   /* the same for every input, live, and into the ini */
/* for on-screen prompts: the display name of what is bound to a Switch input (bit as above) in that slot, or NULL */
const char* bind_key_name(int switch_bit, int slot);

#ifdef __cplusplus
}
#endif
