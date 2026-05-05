#include "wwl.h"

int main(void) {
    struct wwl_state *state = wwl_init(100, 100, "awesome client", 0xAA222222);
    wwl_set_fps(state, 200);

    while (wwl_update(state)) {
        wwl_draw_rect(state, wwl_get_mouse_x(state) - 50, wwl_get_mouse_y(state) - 50, 100, 100, 0xFFFF0000);

        if (is_button_pressed(state, MOUSE_BTN_LEFT)) {
            wwl_set_cursor(state, "cell");
        }

        if (is_button_released(state, MOUSE_BTN_LEFT)) {
            wwl_set_cursor(state, "default");
        }

        if (is_key_pressed(state, KEY_SPACE)) {
            wwl_set_cursor(state, "all-resize");
        }

        if (is_key_released(state, KEY_SPACE)) {
            wwl_set_cursor(state, "default");
        }
    }

    wwl_close(state);
    return 0;
}
