#!/usr/bin/env python3
"""Run the real camera-mode and field-of-view hooks the touch overlay drives.

Extracts director_update_controls (source/camera/director.c) and
player_control_get_field_of_view (source/game/player_control.c), which contain
the Android hooks, and executes them against stubs with the system compiler.
No Android device is needed; only the pure decision logic is exercised.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
out = root / "build/camera-touch-tests"
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


director = (root / "source/camera/director.c").read_text()
player_control = (root / "source/game/player_control.c").read_text()

code = r'''
#define HALO_ANDROID 1
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef int boolean;
typedef unsigned char byte;
typedef float real;
#define FALSE 0
#define TRUE 1
#define NONE (-1)
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define TICKS_PER_SECOND 30
#define DEGREES_TO_RADIANS(d) ((real)((d)*0.017453292519943295f))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define PIN(v,a,b) MIN(MAX(v,a),b)
#define SET_FLAG(v,b,on) do{if(on)(v)|=1u<<(b);else(v)&=~(1u<<(b));}while(0)

enum { _gamepad_analog_button_black, _gamepad_binary_button_right_thumb,
       _gamepad_analog_button_right_trigger, _gamepad_analog_button_left_trigger,
       _gamepad_binary_button_dpad_up, _gamepad_binary_button_dpad_down };
enum { _gamepad_stick_left, _gamepad_stick_right };
enum { _camera_control_forward_bit, _camera_control_reverse_bit, _camera_control_left_bit,
       _camera_control_right_bit, _camera_control_up_bit, _camera_control_down_bit,
       _camera_control_roll_left_bit, _camera_control_roll_right_bit };
enum { _variable_height, _variable_roll, _variable_forward, _variable_right };
enum { _key_backspace, _key_tab, _key_w, _key_s, _key_a, _key_d, _key_r, _key_f, _key_t, _key_g };

typedef void (*director_camera_update_proc)(void);

struct camera_control {
    short local_player_index; boolean active; byte pad3; real seconds_elapsed;
    struct { real yaw, pitch, roll; } facing_delta;
    struct { real i, j, k; } position_delta;
    real wheel_delta;
};
struct director {
    director_camera_update_proc camera_proc; boolean debug_controls; real debug_input_scale;
    struct { real delta; } debug_variables[4];
};
struct gamepad_state { byte buttons[8]; struct { short x, y; } sticks[2]; };
struct mouse_state { long x, y, wheel_delta; byte buttons[2]; };
struct player_datum { short local_player_index; };
struct player_control { short unit_index; short zoom_level; };
struct unit_datum { short definition_index; struct { short current_weapon_index; } unit; };
struct unit_definition { struct { real camera_field_of_view; } unit; };

static struct { real dtime; struct director local_players[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS]; } director_globals;
static struct gamepad_state pad;
static struct mouse_state mouse;
static struct player_datum player;
static struct player_control pc;
static struct unit_datum ud;
static struct unit_definition u_def;
static boolean camera_pending;
static boolean mouse_present;
static float test_fov = 70.0f;
static int key_down[16];
boolean director_camera_switch_fast = FALSE;

struct director *director_get(short i) { return &director_globals.local_players[i]; }
int local_player_get_player_index(short i) { return i; }
struct player_datum *player_get(int i) { return &player; }
int input_has_gamepad(short i) { return !mouse_present; }
struct gamepad_state const *input_get_gamepad_state(short i) { return &pad; }
struct mouse_state const *input_get_mouse_state(void) { return mouse_present ? &mouse : NULL; }
int input_key_is_down(int key) { return key_down[key] ? TRUE : FALSE; }
void director_process_variables(short i, unsigned long flags, real delta) {}
void director_inhibit_input(short i) {}
void director_inhibit_facing(short i) {}
void first_person_camera_update(void) {}
void following_camera_update(void) {}
void flying_camera_update(void) {}
void *csmemset(void *p, int v, unsigned long n) { return memset(p, v, n); }
int host_touch_camera_read(void) { int r = camera_pending; camera_pending = 0; return r; }
float host_touch_field_of_view(void) { return test_fov; }

struct player_control *player_control_get(short i) { return &pc; }
struct unit_datum *unit_get(long i) { return &ud; }
struct unit_definition *unit_definition_get(long i) { return &u_def; }
long unit_inventory_get_weapon(long unit, short weapon) { return weapon == 0 ? NONE : 1; }
real weapon_get_field_of_view(long weapon_index, real base, real zoom) { return base / (1.0f + zoom); }
''' + "\n" + extract(director, "static boolean director_update_controls(") + "\n" + \
    extract(player_control, "real player_control_get_field_of_view(") + r'''

#define NEAR(a,b) (fabsf((a)-(b)) < 0.0001f)

int main(void) {
    struct camera_control controls;
    player.local_player_index = 0;
    director_globals.dtime = 1.f/60.f;
    director_globals.local_players[0].camera_proc = first_person_camera_update;
    director_globals.local_players[0].debug_input_scale = 1.f;

    /* a tap on the overlay's camera button switches once and only once */
    if (director_update_controls(0, &controls)) return 1;
    camera_pending = 1;
    if (!director_update_controls(0, &controls)) return 2;
    if (director_update_controls(0, &controls)) return 3;
    if (camera_pending) return 4;
    /* the button belongs to the first local player only */
    camera_pending = 1;
    if (director_update_controls(1, &controls)) return 5;
    if (!camera_pending) return 6;
    camera_pending = 0;
    /* the gamepad's black button still cycles the camera */
    pad.buttons[_gamepad_analog_button_black] = 30;
    if (!director_update_controls(0, &controls)) return 7;
    pad.buttons[_gamepad_analog_button_black] = 31;
    if (director_update_controls(0, &controls)) return 8;
    /* keyboard menus keep the backspace switch */
    mouse_present = TRUE;
    pad.buttons[_gamepad_analog_button_black] = 0;
    key_down[_key_backspace] = 1;
    if (!director_update_controls(0, &controls)) return 9;
    key_down[_key_backspace] = 0;
    if (director_update_controls(0, &controls)) return 10;
    mouse_present = FALSE;

    /* field of view: the overlay scales the unit's own, the weapon zoom narrows */
    pc.unit_index = 1; pc.zoom_level = 0;
    ud.definition_index = 0; ud.unit.current_weapon_index = 0;
    u_def.unit.camera_field_of_view = DEGREES_TO_RADIANS(70.f);
    test_fov = 70.f;
    real at_70 = player_control_get_field_of_view(0);
    if (!NEAR(at_70, DEGREES_TO_RADIANS(70.f))) return 11;
    test_fov = 90.f;
    real at_90 = player_control_get_field_of_view(0);
    if (!(at_90 > at_70)) return 12;
    if (!NEAR(at_90, DEGREES_TO_RADIANS(90.f))) return 13;
    test_fov = 55.f;
    real at_55 = player_control_get_field_of_view(0);
    if (!(at_55 < at_70)) return 14;
    if (!NEAR(at_55, DEGREES_TO_RADIANS(55.f))) return 15;
    /* a weapon's zoom still narrows whatever the overlay set */
    ud.unit.current_weapon_index = 1;
    pc.zoom_level = 1;
    test_fov = 70.f;
    real zoomed = player_control_get_field_of_view(0);
    if (!(zoomed < at_70)) return 16;
    pc.zoom_level = 0;
    if (!NEAR(player_control_get_field_of_view(0), at_70)) return 17;
    /* the result stays inside the engine's own range */
    ud.unit.current_weapon_index = 0;
    u_def.unit.camera_field_of_view = DEGREES_TO_RADIANS(170.f);
    test_fov = 90.f;
    real clamped = player_control_get_field_of_view(0);
    if (!(clamped <= DEGREES_TO_RADIANS(90.f) + 0.0001f)) return 18;
    printf("Camera tap switching and field of view scaling passed\n");
    return 0;
}
'''

source = out / "test.c"
source.write_text(code)
binary = out / "test"
subprocess.run(["cc", "-std=c99", "-o", str(binary), str(source)], check=True, cwd=str(root))
subprocess.run([str(binary)], check=True)
