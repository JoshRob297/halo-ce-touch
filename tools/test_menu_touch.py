#!/usr/bin/env python3
"""Run the real touch-to-menu pointer mapping and the game keyboard hit test.

Extracts halo_ui_pointer_update (port/linux/src/d3d8_gl.c, the Android branch)
and virtual_keyboard_touch (source/interface/virtual_keyboard.c) and executes
them against a stubbed render target, so the letterbox inversion, the screen
edge steps and the keyboard rectangles are checked without a device.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
out = root / "build/menu-touch-tests"
out.mkdir(parents=True, exist_ok=True)


def extract(source, signature, last=True, after=None):
    """The body of the function whose definition starts at `signature`."""
    base = source.index(after) if after else 0
    index = source.find(signature, base)
    if index < 0:
        raise ValueError(signature)
    if last:
        while True:
            following = source.find(signature, index + 1)
            if following < 0:
                break
            index = following
    paren = index + len(signature) - 1
    depth, cursor = 0, paren
    while True:
        if source[cursor] == "(":
            depth += 1
        elif source[cursor] == ")":
            depth -= 1
            if depth == 0:
                break
        cursor += 1
    cursor += 1
    while source[cursor] in " \t\r\n":
        cursor += 1
    if source[cursor] != "{":
        raise ValueError("not a definition: " + signature)
    depth, end = 0, cursor
    while True:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[index:end + 1]
        end += 1


gl = (root / "port/linux/src/d3d8_gl.c").read_text()
keyboard = (root / "source/interface/virtual_keyboard.c").read_text()

code = r'''
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "halo_ui_pointer.h"

typedef int boolean;
#define FALSE 0
#define TRUE 1
#define NUMBER_OF_VIRTUAL_KEYS 4
#define VIRTUAL_KEYBOARD_ROW_COUNT 2
#define VIRTUAL_KEYBOARD_COLUMN_COUNT 2
#define _event_key_select 7

struct render_target_entry {
    struct { int width, height, gl_width, gl_height; } target;
};
static struct { struct render_target_entry back_buffer; } device;
struct render_target_entry *render_target_get(struct render_target_entry *entry) { return entry; }

static int drawable_width = 1280, drawable_height = 720;
void platform_video_drawable_size(int *width, int *height) {
    *width = drawable_width; *height = drawable_height;
}
static long ui_screen_width = 640;
long halo_screen_width(void) { return ui_screen_width; }
static int menus_set_active;
void platform_menus_set_active(int active) { menus_set_active = active; }

/* mirrors port/android/host/host_touch.c: copy the frame's point, clear the edges */
static float ui_point[6];
void host_touch_pointer_read(float *point) {
    memcpy(point, ui_point, sizeof(ui_point));
    ui_point[2] = ui_point[3] = ui_point[4] = ui_point[5] = 0;
}
''' + "\n" + extract(gl, "int halo_ui_pointer_update(", last=False, after="host_touch_pointer_read") + "\n" + \
    r'''
typedef struct { short y0, x0, y1, x1; } rectangle2d;
static rectangle2d keyboard_rect[NUMBER_OF_VIRTUAL_KEYS] = {
    { 10, 10, 30, 30 }, { 10, 40, 30, 60 }, { 40, 10, 60, 30 }, { 40, 40, 60, 60 }
};
static int virtual_keyboard_layout_table[VIRTUAL_KEYBOARD_ROW_COUNT][VIRTUAL_KEYBOARD_COLUMN_COUNT] = {
    { 0, 1 }, { 2, 3 }
};
static struct { boolean active; long row, column, last_event; } virtual_keyboard_globals;
static int selected_key, cancel_calls;
static void virtual_keyboard_select(void) {
    selected_key =
        virtual_keyboard_layout_table[virtual_keyboard_globals.row][virtual_keyboard_globals.column] + 1;
}
static void virtual_keyboard_cancel(void) { cancel_calls++; }
''' + "\n" + extract(keyboard, "void virtual_keyboard_touch(") + r'''

#define NEAR(a,b) (fabsf((a)-(b)) < 0.01f)

static struct halo_ui_pointer pointer;

int main(void) {
    device.back_buffer.target.width = 640;
    device.back_buffer.target.height = 480;
    device.back_buffer.target.gl_width = 640;
    device.back_buffer.target.gl_height = 480;

    /* no menu, no pointer */
    ui_point[0] = ui_point[1] = 0.5f; ui_point[2] = 1;
    if (halo_ui_pointer_update(0, &pointer)) return 1;

    /* a tap in the middle of a 1280x720 letterboxed 4:3 surface lands at 320x240 */
    ui_screen_width = 640;
    ui_point[0] = 0.5f; ui_point[1] = 0.5f; ui_point[2] = 1; ui_point[3] = 0;
    if (!halo_ui_pointer_update(1, &pointer)) return 2;
    if (!(pointer.x == 320 && pointer.y == 240)) return 3;
    if (!(pointer.click_x == 320 && pointer.click_y == 240)) return 4;
    if (!pointer.left_clicks) return 5;
    if (!menus_set_active) return 6;
    /* the click is consumed with the frame */
    halo_ui_pointer_update(1, &pointer);
    if (pointer.left_clicks) return 7;

    /* widescreen menus centre a 640-wide space inside the drawn width */
    ui_screen_width = 1068;
    ui_point[0] = 0.5f; ui_point[1] = 0.0f; ui_point[2] = 0; ui_point[3] = 0;
    halo_ui_pointer_update(1, &pointer);
    if (!(pointer.x == (short)(320 - (1068 - 640) / 2))) return 8;

    /* the screen edges step the focused list, the middle does not */
    ui_screen_width = 640;
    ui_point[0] = 0.10f; ui_point[1] = 0.5f;
    halo_ui_pointer_update(1, &pointer);
    if (pointer.side_step != -1) return 9;
    ui_point[0] = 0.90f;
    halo_ui_pointer_update(1, &pointer);
    if (pointer.side_step != 1) return 10;
    ui_point[0] = 0.50f;
    halo_ui_pointer_update(1, &pointer);
    if (pointer.side_step != 0) return 11;

    /* a swipe becomes scroll pixels in the menu's own height, and the drag is flagged */
    ui_point[0] = 0.5f; ui_point[1] = 0.5f; ui_point[4] = 0.25f; ui_point[5] = 1;
    halo_ui_pointer_update(1, &pointer);
    if (pointer.scroll_pixels != (short)(0.25f * 480)) return 12;
    if (!pointer.scroll_drag) return 13;
    /* the scroll is consumed with the frame */
    halo_ui_pointer_update(1, &pointer);
    if (pointer.scroll_pixels || pointer.scroll_drag) return 14;

    /* the game's keyboard selects the key under the tap, and cancels on back */
    virtual_keyboard_globals.active = TRUE;
    if (selected_key) return 15;
    virtual_keyboard_touch(50, 50, TRUE, FALSE);
    if (selected_key != 4) return 16;
    if (!(virtual_keyboard_globals.row == 1 && virtual_keyboard_globals.column == 1)) return 17;
    if (!(virtual_keyboard_globals.last_event == _event_key_select)) return 18;
    selected_key = 0;
    virtual_keyboard_touch(20, 20, TRUE, FALSE);
    if (selected_key != 1) return 19;
    selected_key = 0;
    virtual_keyboard_touch(100, 100, TRUE, FALSE);
    if (selected_key) return 20;
    virtual_keyboard_touch(20, 20, TRUE, TRUE);
    if (cancel_calls != 1) return 21;
    virtual_keyboard_globals.active = FALSE;
    selected_key = 0;
    virtual_keyboard_touch(20, 20, TRUE, FALSE);
    if (selected_key) return 22;

    printf("Menu pointer mapping, edge steps and keyboard taps passed\n");
    return 0;
}
'''

source = out / "test.c"
source.write_text(code)
binary = out / "test"
subprocess.run(["cc", "-std=c99", "-idirafter", str(root / "port/linux/include"),
                "-o", str(binary), str(source)], check=True, cwd=str(root))
subprocess.run([str(binary)], check=True)
