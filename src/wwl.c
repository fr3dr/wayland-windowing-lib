#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <wayland-client-protocol.h>
#include <wayland-client-core.h>
#include "xdg-shell-client-protocol.h"
#include "pointer-constraints-client-protocol.h"
#include "relative-pointer-client-protocol.h"
#include "wwl.h"

#define MAX_OUTPUT_COUNT 16

// define evdev mouse button codes
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114

int create_shm_file(size_t size) {
    char name[255] = "/";
    for (int i = 1; i < 255; i++) {
        name[i] = (double)rand() / (double)RAND_MAX * 26 + 'a';
    }

    int fd = shm_open(name, O_RDWR | O_EXCL | O_CREAT, 0600);
    if (fd == -1) {
        exit(errno);
    }

    assert(shm_unlink(name) != -1);

    if (ftruncate(fd, size) == -1) {
        exit(errno);
    }

    return fd;
}

enum pointer_event_mask {
    POINTER_EVENT_ENTER = 1 << 0,
    POINTER_EVENT_LEAVE = 1 << 1,
    POINTER_EVENT_MOTION = 1 << 2,
    POINTER_EVENT_BUTTON = 1 << 3,
    POINTER_EVENT_AXIS = 1 << 4,
    POINTER_EVENT_AXIS_SOURCE = 1 << 5,
    POINTER_EVENT_AXIS_STOP = 1 << 6,
    POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
    POINTER_EVENT_AXIS_VALUE120 = 1 << 8,
    POINTER_EVENT_AXIS_DIRECTION = 1 << 9,
};

struct pointer_event {
    uint32_t event_mask;
    wl_fixed_t surface_x;
    wl_fixed_t surface_y;
    uint32_t button;
    uint32_t button_state;
    uint32_t time;
    uint32_t serial;
    struct {
        bool valid;
        wl_fixed_t value;
        int32_t discrete;
        int32_t value120;
        uint32_t direction;
    } axis[2];
    uint32_t axis_source;
};

struct toplevel_configure_event {
    int32_t width, height;
    bool resizing;
};

struct wwl_state {
    // globals
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    struct wl_shm *wl_shm;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_seat *wl_seat;
    struct wl_output *wl_outputs[MAX_OUTPUT_COUNT];
    int wl_output_count;
    struct zwp_pointer_constraints_v1 *pointer_constraints;
    struct zwp_relative_pointer_manager_v1 *relative_pointer_manager;

    // wayland objects
    struct wl_shm_pool *wl_shm_pool;
    struct wl_buffer *wl_buffer;
    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct toplevel_configure_event toplevel_configure_event;

    struct wl_pointer *wl_pointer;
    struct pointer_event pointer_event;
    struct zwp_confined_pointer_v1 *zwp_confined_pointer;
    struct zwp_relative_pointer_v1 *zwp_relative_pointer;

    struct wl_cursor_theme *wl_cursor_theme;
    struct wl_surface *cursor_surface;
    int cursor_hotspot_x;
    int cursor_hotspot_y;

    struct wl_keyboard *wl_keyboard;

    // library state
    bool running;
    int32_t width, height;
    int32_t stride;

    int fps;
    double target_frame_time;
    double frame_time;
    struct timespec frame_start, frame_end;

    uint32_t *draw_buffer;

    int shm_fd;
    int shm_size;
    uint32_t *shm_data;

    bool pointer_active;
    uint32_t pointer_serial;

    double mouse_x;
    double mouse_y;
    double mouse_motion_x;
    double mouse_motion_y;
    uint32_t mouse_button_state;
    uint32_t previous_mouse_button_state;

    bool key_states[MAX_KEY_COUNT];
    bool previous_key_states[MAX_KEY_COUNT];
};

static void commit_frame(struct wwl_state *state) {
    memcpy(state->shm_data, state->draw_buffer, state->shm_size);

    wl_surface_attach(state->wl_surface, state->wl_buffer, 0, 0);
    wl_surface_damage(state->wl_surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(state->wl_surface);
}

static void wl_output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform) {
}

static void wl_output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
}

static void wl_output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
}

static void wl_output_name(void *data, struct wl_output *wl_output, const char *name) {
}

static void wl_output_description(void *data, struct wl_output *wl_output, const char *description) {
}

static void wl_output_done(void *data, struct wl_output *wl_output) {
}

const struct wl_output_listener wl_output_listener = {
    .geometry = wl_output_geometry,
    .mode = wl_output_mode,
    .scale = wl_output_scale,
    .name = wl_output_name,
    .description = wl_output_description,
    .done = wl_output_done,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct wwl_state *state = data;
    // fprintf(stderr, "xdg surface configure\n");
    xdg_surface_ack_configure(xdg_surface, serial);

    if (state->toplevel_configure_event.resizing) {
        state->width = state->toplevel_configure_event.width;
        state->height = state->toplevel_configure_event.height;
        state->stride = state->width * 4;

        munmap(state->shm_data, state->shm_size);
        state->shm_size = state->stride * state->height;
        fprintf(stderr, "shm size: %d\n", state->shm_size);

        close(state->shm_fd);
        state->shm_fd = create_shm_file(state->shm_size);
        state->shm_data = mmap(NULL, state->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, state->shm_fd, 0);
        if (state->shm_data == MAP_FAILED) {
            exit(errno);
        }

        wl_buffer_destroy(state->wl_buffer);
        wl_shm_pool_destroy(state->wl_shm_pool);
        state->wl_shm_pool = wl_shm_create_pool(state->wl_shm, state->shm_fd, state->shm_size);
        state->wl_buffer = wl_shm_pool_create_buffer(state->wl_shm_pool, 0, state->width, state->height, state->stride, WL_SHM_FORMAT_ARGB8888);

        state->draw_buffer = realloc(state->draw_buffer, state->shm_size);

        xdg_surface_set_window_geometry(state->xdg_surface, 0, 0, state->width, state->height);
    }

    memset(&state->toplevel_configure_event, 0, sizeof(state->toplevel_configure_event));
}

const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct wwl_state *state = data;
    state->running = false;
}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
    struct wwl_state *state = data;

    enum xdg_toplevel_state *s;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_RESIZING) {
            state->toplevel_configure_event.resizing = true;
        }
    }

    if (width > 0 && height > 0) {
        if (width != state->width || height != state->height) {
            state->toplevel_configure_event.resizing = true;
        }
    }

    state->toplevel_configure_event.width = width;
    state->toplevel_configure_event.height = height;
}

static void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
}

static void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {
}

const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .close = xdg_toplevel_close,
    .configure = xdg_toplevel_configure,
    .configure_bounds = xdg_toplevel_configure_bounds,
    .wm_capabilities = xdg_toplevel_wm_capabilities,
};

static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_ENTER;
    state->pointer_event.serial = serial;
    state->pointer_event.surface_x = surface_x;
    state->pointer_event.surface_y = surface_y;
}

static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
    state->pointer_event.serial = serial;
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_MOTION;
    state->pointer_event.time = time;
    state->pointer_event.surface_x = surface_x;
    state->pointer_event.surface_y = surface_y;
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t button_state) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
    state->pointer_event.time = time;
    state->pointer_event.button = button;
    state->pointer_event.button_state = button_state;
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_AXIS;
    state->pointer_event.time = time;
    state->pointer_event.axis[axis].valid = true;
    state->pointer_event.axis[axis].value = value;
}

static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
    state->pointer_event.axis_source = axis_source;
}

static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
    state->pointer_event.time = time;
    state->pointer_event.axis[axis].valid = true;
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
    state->pointer_event.axis[axis].valid = true;
    state->pointer_event.axis[axis].discrete = discrete;
}

static void wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_AXIS_VALUE120;
    state->pointer_event.axis[axis].valid = true;
    state->pointer_event.axis[axis].value120 = value120;
}

static void wl_pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction) {
    struct wwl_state *state = data;
    state->pointer_event.event_mask |= POINTER_EVENT_AXIS_DIRECTION;
    state->pointer_event.axis[axis].valid = true;
    state->pointer_event.axis[axis].direction = direction;
}

static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
    struct wwl_state *state = data;
    struct pointer_event *event = &state->pointer_event;

    if (event->event_mask & POINTER_EVENT_ENTER) {
        state->pointer_active = true;
        state->pointer_serial = event->serial;
        wl_pointer_set_cursor(state->wl_pointer, state->pointer_serial, state->cursor_surface, state->cursor_hotspot_x, state->cursor_hotspot_y);
    }

    if (event->event_mask & POINTER_EVENT_LEAVE) {
        state->pointer_active = false;
    }

    if (event->event_mask & POINTER_EVENT_MOTION) {
        state->mouse_x = wl_fixed_to_double(event->surface_x);
        state->mouse_y = wl_fixed_to_double(event->surface_y);
    }

    if (event->event_mask & POINTER_EVENT_BUTTON) {
        if (event->button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
            if (event->button == BTN_LEFT) {
                state->mouse_button_state |= MOUSE_BTN_LEFT;
            } else if (event->button == BTN_RIGHT) {
                state->mouse_button_state |= MOUSE_BTN_RIGHT;
            } else if (event->button == BTN_MIDDLE) {
                state->mouse_button_state |= MOUSE_BTN_MIDDLE;
            } else if (event->button == BTN_SIDE) {
                state->mouse_button_state |= MOUSE_BTN_SIDE;
            } else if (event->button == BTN_EXTRA) {
                state->mouse_button_state |= MOUSE_BTN_EXTRA;
            }
        } else if (event->button_state == WL_POINTER_BUTTON_STATE_RELEASED) {
            if (event->button == BTN_LEFT) {
                state->mouse_button_state &= ~MOUSE_BTN_LEFT;
            } else if (event->button == BTN_RIGHT) {
                state->mouse_button_state &= ~MOUSE_BTN_RIGHT;
            } else if (event->button == BTN_MIDDLE) {
                state->mouse_button_state &= ~MOUSE_BTN_MIDDLE;
            } else if (event->button == BTN_SIDE) {
                state->mouse_button_state &= ~MOUSE_BTN_SIDE;
            } else if (event->button == BTN_EXTRA) {
                state->mouse_button_state &= ~MOUSE_BTN_EXTRA;
            }
        }
        // printf("mouse button state: %08b\n", state->mouse_button_state);
    }

    memset(event, 0, sizeof(*event));
}

static void zwp_relative_pointer_relative_motion(void *data, struct zwp_relative_pointer_v1 *zwp_relative_pointer, uint32_t utime_hi, uint32_t utime_lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
    struct wwl_state *state = data;

    state->mouse_motion_x += wl_fixed_to_double(dx);
    state->mouse_motion_y += wl_fixed_to_double(dy);
}

const struct zwp_relative_pointer_v1_listener zwp_relative_pointer_listener = {
    .relative_motion = zwp_relative_pointer_relative_motion,
};

const struct wl_pointer_listener wl_pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
    .axis_source = wl_pointer_axis_source,
    .axis_stop = wl_pointer_axis_stop,
    .axis_discrete = wl_pointer_axis_discrete,
    .axis_value120 = wl_pointer_axis_value120,
    .axis_relative_direction = wl_pointer_axis_relative_direction,
    .frame = wl_pointer_frame,
};

void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
}

void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
}

void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) {
}

void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t key_state) {
    struct wwl_state *state = data;

    assert(key < MAX_KEY_COUNT);
    if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        state->key_states[key] = true;
    } else if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        state->key_states[key] = false;
    }
}

void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
}

void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {
}

const struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap = wl_keyboard_keymap,
    .enter = wl_keyboard_enter,
    .leave = wl_keyboard_leave,
    .key = wl_keyboard_key,
    .modifiers = wl_keyboard_modifiers,
    .repeat_info = wl_keyboard_repeat_info,
};

static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
    struct wwl_state *state = data;

    bool has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && state->wl_pointer == NULL) {
        state->wl_pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(state->wl_pointer, &wl_pointer_listener, state);
        state->zwp_relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(state->relative_pointer_manager, state->wl_pointer);
        zwp_relative_pointer_v1_add_listener(state->zwp_relative_pointer, &zwp_relative_pointer_listener, state);
    } else if (!has_pointer && state->wl_pointer != NULL) {
        wl_pointer_destroy(state->wl_pointer);
        state->wl_pointer = NULL;
        zwp_relative_pointer_v1_destroy(state->zwp_relative_pointer);
        state->zwp_relative_pointer = NULL;
    }

    bool has_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
    if (has_keyboard && state->wl_keyboard == NULL) {
        state->wl_keyboard = wl_seat_get_keyboard(wl_seat);
        wl_keyboard_add_listener(state->wl_keyboard, &wl_keyboard_listener, state);
    } else if (!has_keyboard && state->wl_keyboard != NULL) {
        wl_keyboard_destroy(state->wl_keyboard);
        state->wl_keyboard = NULL;
    }
}

static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
    fprintf(stderr, "seat name: %s\n", name);
}

const struct wl_seat_listener wl_seat_listener = {
    .capabilities = wl_seat_capabilities,
    .name = wl_seat_name,
};

void wl_registry_global(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version) {
    struct wwl_state *state = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->wl_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->wl_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, version);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(state->wl_seat, &wl_seat_listener, state);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        state->wl_outputs[state->wl_output_count] = wl_registry_bind(wl_registry, name, &wl_output_interface, version);
        wl_output_add_listener(state->wl_outputs[state->wl_output_count], &wl_output_listener, state);
        state->wl_output_count++;
    } else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        state->pointer_constraints = wl_registry_bind(wl_registry, name, &zwp_pointer_constraints_v1_interface, version);
    } else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        state->relative_pointer_manager = wl_registry_bind(wl_registry, name, &zwp_relative_pointer_manager_v1_interface, version);
    }
}

void wl_registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name) {
}

const struct wl_registry_listener wl_registry_listener = {
    .global = wl_registry_global,
    .global_remove = wl_registry_global_remove,
};

struct wwl_state* wwl_init(int width, int height, const char *title) {
    struct wwl_state *state = calloc(1, sizeof(struct wwl_state));
    state->running = true;
    state->width = width;
    state->height = height;
    state->stride = width * 4;
    state->fps = 0;
    state->target_frame_time = 0;

    state->wl_display = wl_display_connect(NULL);
    state->wl_registry = wl_display_get_registry(state->wl_display);
    wl_registry_add_listener(state->wl_registry, &wl_registry_listener, state);
    wl_display_roundtrip(state->wl_display);

    state->wl_surface = wl_compositor_create_surface(state->wl_compositor);
    state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, state->wl_surface);
    xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);
    state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
    xdg_toplevel_add_listener(state->xdg_toplevel, &xdg_toplevel_listener, state);
    xdg_toplevel_set_title(state->xdg_toplevel, title);
    xdg_toplevel_set_app_id(state->xdg_toplevel, title);
    wl_surface_commit(state->wl_surface);

    state->shm_size = state->stride * height;
    state->shm_fd = create_shm_file(state->shm_size);
    state->shm_data = mmap(NULL, state->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, state->shm_fd, 0);
    if (state->shm_data == MAP_FAILED) {
        fprintf(stderr, "failed to map shm data, fd: (%d)\n", state->shm_fd);
        return NULL;
    }

    state->draw_buffer = malloc(state->shm_size);

    state->wl_shm_pool = wl_shm_create_pool(state->wl_shm, state->shm_fd, state->shm_size);
    state->wl_buffer = wl_shm_pool_create_buffer(state->wl_shm_pool, 0, state->width, state->height, state->stride, WL_SHM_FORMAT_ARGB8888);

    // get xcursor theme and size
    const char *xcursor_theme = getenv("XCURSOR_THEME");
    int cursor_size = 24;
    errno = 0;
    char *end;
    int xcursor_size = (int)strtol(getenv("XCURSOR_SIZE"), &end, 10);
    if (errno == 0 && *end == '\0' && xcursor_size > 0) {
        cursor_size = xcursor_size;
    }

    state->wl_cursor_theme = wl_cursor_theme_load(xcursor_theme, cursor_size, state->wl_shm);
    state->cursor_surface = wl_compositor_create_surface(state->wl_compositor);
    wwl_set_cursor(state, "default");

    wl_display_roundtrip(state->wl_display);

    return state;
}

int wwl_update(struct wwl_state *state) {
    if (!state->running) {
        return 0;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &state->frame_start);

    while (wl_display_prepare_read(state->wl_display) != 0) {
        wl_display_dispatch_pending(state->wl_display);
    }
    wl_display_flush(state->wl_display);
    wl_display_read_events(state->wl_display);
    wl_display_dispatch_pending(state->wl_display);

    return 1;
}

void wwl_update_end(struct wwl_state *state) {
    commit_frame(state);

    state->mouse_motion_x = 0;
    state->mouse_motion_y = 0;

    state->previous_mouse_button_state = state->mouse_button_state;
    for (int i = 0; i < MAX_KEY_COUNT; i++) {
        state->previous_key_states[i] = state->key_states[i];
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &state->frame_end);
    state->frame_time = (state->frame_end.tv_sec - state->frame_start.tv_sec) + (state->frame_end.tv_nsec - state->frame_start.tv_nsec) / 1000000000.0;
    if (state->frame_time < state->target_frame_time) {
        double sleep_seconds = state->target_frame_time - state->frame_time;
        time_t sec = sleep_seconds;
        long nsec = (sleep_seconds - sec) * 1000000000L;
        struct timespec req;
        req.tv_sec = sec;
        req.tv_nsec = nsec;
        while (nanosleep(&req, &req) == -1) continue;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &state->frame_end);
    state->frame_time = (state->frame_end.tv_sec - state->frame_start.tv_sec) + (state->frame_end.tv_nsec - state->frame_start.tv_nsec) / 1000000000.0;
    // fprintf(stderr, "fps: %f\n", 1.0 / state->frame_time);
}

void wwl_close(struct wwl_state *state) {
    munmap(state->shm_data, state->shm_size);
    close(state->shm_fd);
    wl_shm_pool_destroy(state->wl_shm_pool);
    wl_buffer_destroy(state->wl_buffer);
    free(state->draw_buffer);

    zwp_relative_pointer_v1_destroy(state->zwp_relative_pointer);
    zwp_relative_pointer_manager_v1_destroy(state->relative_pointer_manager);
    if (state->zwp_confined_pointer != NULL) {
        zwp_confined_pointer_v1_destroy(state->zwp_confined_pointer);
    }
    zwp_pointer_constraints_v1_destroy(state->pointer_constraints);
    wl_pointer_destroy(state->wl_pointer);
    wl_cursor_theme_destroy(state->wl_cursor_theme);

    wl_keyboard_destroy(state->wl_keyboard);

    xdg_toplevel_destroy(state->xdg_toplevel);
    xdg_surface_destroy(state->xdg_surface);
    wl_surface_destroy(state->wl_surface);

    xdg_wm_base_destroy(state->xdg_wm_base);
    wl_seat_destroy(state->wl_seat);
    wl_compositor_destroy(state->wl_compositor);
    wl_shm_destroy(state->wl_shm);
    wl_registry_destroy(state->wl_registry);
    wl_display_disconnect(state->wl_display);

    free(state);
    state = NULL;
}

void wwl_set_fps(struct wwl_state *state, int fps) {
    if (fps <= 0) {
        state->fps = 0;
        state->target_frame_time = 0;
        return;
    }

    state->fps = fps;
    state->target_frame_time = (1.0 / fps);
    fprintf(stderr, "target frame time: %f\n", state->target_frame_time);
}

double wwl_get_deltatime(struct wwl_state *state) {
    return state->frame_time;
}

void wwl_set_min_size(struct wwl_state *state, int32_t width, int32_t height) {
    xdg_toplevel_set_min_size(state->xdg_toplevel, width, height);
    wl_surface_commit(state->wl_surface);
}

void wwl_set_max_size(struct wwl_state *state, int32_t width, int32_t height) {
    xdg_toplevel_set_max_size(state->xdg_toplevel, width, height);
    wl_surface_commit(state->wl_surface);
}

void wwl_clear_background(struct wwl_state *state, uint32_t color) {
    for (int y = 0; y < state->height; y++) {
        for (int x = 0; x < state->width; x++) {
            state->draw_buffer[y * state->width + x] = color;
        }
    }
}

void wwl_draw_pixel(struct wwl_state *state, int x, int y, uint32_t pixel) {
    if (x >= state->width || x < 0 || y >= state->height || y < 0) {
        return;
    }

    state->draw_buffer[y * state->width + x] = pixel;
}

void wwl_draw_rect(struct wwl_state *state, int x, int y, int width, int height, uint32_t color) {
    for (int loop_y = y; loop_y < y + height; loop_y++) {
        if (loop_y < 0) {
            continue;
        }
        if (loop_y >= state->height) {
            return;
        }
        for (int loop_x = x; loop_x < x + width; loop_x++) {
            if (loop_x < 0) {
                continue;
            }
            if (loop_x >= state->width) {
                break;
            }

            state->draw_buffer[loop_y * state->width + loop_x] = color;
        }
    }
}

void wwl_set_cursor(struct wwl_state *state, const char *cursor) {
    if (cursor == NULL) {
        wl_surface_attach(state->cursor_surface, NULL, 0, 0);

        state->cursor_hotspot_x = 0;
        state->cursor_hotspot_y = 0;
    } else {
        struct wl_cursor *cur = wl_cursor_theme_get_cursor(state->wl_cursor_theme, cursor);
        struct wl_buffer *buf = wl_cursor_image_get_buffer(cur->images[0]);

        wl_surface_attach(state->cursor_surface, buf, 0, 0);

        state->cursor_hotspot_x = cur->images[0]->hotspot_x;
        state->cursor_hotspot_y = cur->images[0]->hotspot_y;
    }

    wl_surface_damage(state->cursor_surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(state->cursor_surface);

    if (state->pointer_active) {
        wl_pointer_set_cursor(state->wl_pointer, state->pointer_serial, state->cursor_surface, state->cursor_hotspot_x, state->cursor_hotspot_y);
    }
}

void wwl_lock_cursor(struct wwl_state *state) {
    if (state->zwp_confined_pointer == NULL) {
        state->zwp_confined_pointer = zwp_pointer_constraints_v1_confine_pointer(state->pointer_constraints, state->wl_surface, state->wl_pointer, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    }
}

void wwl_unlock_cursor(struct wwl_state *state) {
    if (state->zwp_confined_pointer != NULL) {
        zwp_confined_pointer_v1_destroy(state->zwp_confined_pointer);
        state->zwp_confined_pointer = NULL;
    }
}

double wwl_get_mouse_x(struct wwl_state *state) {
    return state->mouse_x;
}

double wwl_get_mouse_y(struct wwl_state *state) {
    return state->mouse_y;
}

double wwl_get_mouse_motion_x(struct wwl_state *state) {
    return state->mouse_motion_x;
}
double wwl_get_mouse_motion_y(struct wwl_state *state) {
    return state->mouse_motion_y;
}

bool wwl_is_button_pressed(struct wwl_state *state, uint32_t button) {
    return (state->previous_mouse_button_state & button) == 0 && (state->mouse_button_state & button);
}

bool wwl_is_button_down(struct wwl_state *state, uint32_t button) {
    return state->mouse_button_state & button;
}

bool wwl_is_button_released(struct wwl_state *state, uint32_t button) {
    return (state->previous_mouse_button_state & button) && (state->mouse_button_state & button) == 0;
}

bool wwl_is_key_pressed(struct wwl_state *state, uint32_t key) {
    if (key >= MAX_KEY_COUNT) {
        return false;
    }

    return state->key_states[key] && !state->previous_key_states[key];
}

bool wwl_is_key_down(struct wwl_state *state, uint32_t key) {
    if (key >= MAX_KEY_COUNT) {
        return false;
    }

    return state->key_states[key];
}

bool wwl_is_key_released(struct wwl_state *state, uint32_t key) {
    if (key >= MAX_KEY_COUNT) {
        return false;
    }

    return !state->key_states[key] && state->previous_key_states[key];
}
