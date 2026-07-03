#include "wwl.h"
#include <stdio.h>

int main(void) {
    struct wwl_state *state = wwl_init(1000, 1000, "awesome client");
    wwl_set_fps(state, 60);
    wwl_set_min_size(state, 1000, 1000);
    wwl_set_max_size(state, 1000, 1000);

    while (wwl_update(state)) {
        wwl_clear_background(state, 0xFF222222);
        wwl_draw_rect(state, 200, 200, 600, 600, 0xFF0000FF);
        wwl_draw_rect(state, wwl_get_mouse_x(state) - 50, wwl_get_mouse_y(state) - 50, 100, 100, 0xFFFF0000);

        if (wwl_is_button_pressed(state, MOUSE_BTN_LEFT)) {
            wwl_set_cursor(state, "cell");
        }

        if (wwl_is_button_released(state, MOUSE_BTN_LEFT)) {
            wwl_set_cursor(state, "default");
        }

        if (wwl_is_key_pressed(state, KEY_SPACE)) {
            wwl_set_cursor(state, "all-resize");
        }

        if (wwl_is_key_released(state, KEY_SPACE)) {
            wwl_set_cursor(state, "default");
        }

        if (wwl_is_key_pressed(state, KEY_H)) {
            wwl_set_cursor(state, NULL);
        }

        if (wwl_is_key_released(state, KEY_H)) {
            wwl_set_cursor(state, "default");
        }

        if (wwl_is_key_released(state, KEY_L)) {
            wwl_lock_cursor(state);
        }

        if (wwl_is_key_released(state, KEY_U)) {
            wwl_unlock_cursor(state);
        }

        wwl_update_end(state);
    }

    wwl_close(state);
    return 0;
}
