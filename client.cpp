#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdarg>
#include <cstring>
#include <iostream>
#include <string>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

using namespace std;

int gensym_count;

string gensym(string prefix) {
  auto count_str = to_string(gensym_count);
  prefix.append(count_str);
  return prefix;
}

// Shared Memory

static int create_shm_file() {
  cout << "[Log] Creating shm file..." << endl;
  auto name = gensym("/wl_shm-");
  int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  shm_unlink(name.c_str());
  return fd;
}

int allocate_shm_file(size_t size) {
  int fd = create_shm_file();
  if (fd < 0)
    return -1;
  cout << "[Log] Truncating shm file..." << endl;
  if (ftruncate(fd, size) == -1) {
    close(fd);
    return -1;
  }
  return fd;
}

// Wayland

struct state {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct wl_seat *seat;

  struct xdg_wm_base *xdg_wm_base;

  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;

  uint32_t last_frame = -1;
  bool bg_lighten = true;
  uint8_t bg_grayscale = 0;
  bool closed = false;
};

// Drawing buffer

const struct wl_buffer_listener buffer_listener = {
    .release = [](void *data, struct wl_buffer *buffer) {
      wl_buffer_destroy(buffer);
    }};

struct wl_buffer *draw_buffer(struct state *state) {
  const int width = 640, height = 480;
  const int stride = width * 4;
  const int pool_size = stride * height * 2;

  cout << "[Log] Allocate shm...\n";
  int fd = allocate_shm_file(pool_size);
  cout << "[Log] Creating pool...\n";
  auto pool_data = (uint8_t *)mmap(NULL, pool_size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
  auto pool = wl_shm_create_pool(state->shm, fd, pool_size);

  cout << "[Log] Making buffer...\n";
  int index = 0;
  int offset = stride * height * index;
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, offset, width, height, stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  cout << "[Log] Set pixels...\n";
  uint32_t *pixels = (uint32_t *)&pool_data[offset];
  cout << "[Log] Grayscale: " << to_string(state->bg_grayscale) << endl;
  memset(pixels, state->bg_grayscale, width * height * 4);

  munmap(pool_data, pool_size);

  wl_buffer_add_listener(buffer, &buffer_listener, NULL);
  return buffer;
}

// Surface listener

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure =
        [](void *data, struct xdg_surface *surface, uint32_t serial) {
          xdg_surface_ack_configure(surface, serial);

          struct wl_buffer *buffer = draw_buffer((struct state *)data);

          struct state *state = (struct state *)data;
          wl_surface_attach(state->surface, buffer, 0, 0);
          wl_surface_damage_buffer(state->surface, 0, 0, INT32_MAX, INT32_MAX);
          wl_surface_commit(state->surface);
        },
};

// WL callback

static const struct wl_callback_listener frame_listener = {
    .done = [](void *data, struct wl_callback *callback, uint32_t time) {
      cout << "[log] Callback fired." << endl;
      wl_callback_destroy(callback);
      auto state = static_cast<struct state *>(data);
      callback = wl_surface_frame(state->surface);
      wl_callback_add_listener(callback, &frame_listener, state);

      if (state->last_frame > 0) {
        // auto elapsed = (int)((time - state->last_frame) / 1000);
        if (state->bg_lighten) {
          if (state->bg_grayscale >= 255)
            state->bg_lighten = false;
          else
            state->bg_grayscale = min(state->bg_grayscale + 1, 255);
        } else {
          if (state->bg_grayscale <= 0)
            state->bg_lighten = true;
          else
            state->bg_grayscale = max(state->bg_grayscale - 1, 0);
        }
      }

      auto buffer = draw_buffer(state);
      wl_surface_attach(state->surface, buffer, 0, 0);
      wl_surface_damage_buffer(state->surface, 0, 0, INT32_MAX, INT32_MAX);
      wl_surface_commit(state->surface);
      state->last_frame = time;
    }};

// Seat & Pointer

static const struct wl_pointer_listener pointer_listener = {
    .enter = [](void *data, struct wl_pointer *wl_pointer, uint32_t serial,
                struct wl_surface *surface, wl_fixed_t surface_x,
                wl_fixed_t surface_y) {},
    .leave = [](void *data, struct wl_pointer *wl_pointer, uint32_t serial,
                struct wl_surface *surface) {},
    .motion = [](void *data, struct wl_pointer *wl_pointer, uint32_t time,
                 wl_fixed_t surface_x, wl_fixed_t surface_y) {},
    .button =
        [](void *data, struct wl_pointer *wl_pointer, uint32_t serial,
           uint32_t time, uint32_t button, uint32_t state_num) {
          auto s = static_cast<state *>(data);
          switch (button) {
          case BTN_RIGHT:
            xdg_toplevel_destroy(s->xdg_toplevel);
            xdg_surface_destroy(s->xdg_surface);
            xdg_wm_base_destroy(s->xdg_wm_base);
            wl_surface_destroy(s->surface);
            s->closed = true;
            break;
          case BTN_LEFT:
            xdg_toplevel_move(s->xdg_toplevel, s->seat, serial);
          default:
            break;
          }
          if (button == BTN_LEFT) {
          }
          // wl_display_disconnect(s->display);
        },
    .axis = [](void *data, struct wl_pointer *wl_pointer, uint32_t time,
               uint32_t axis, wl_fixed_t value) {},
    .frame = [](void *data, struct wl_pointer *wl_pointer) {},
};

// Registry

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = [](void *data, struct xdg_wm_base *xdg_wm_base,
               uint32_t serial) { xdg_wm_base_pong(xdg_wm_base, serial); },
};

static void reg_handle_global(void *data, struct wl_registry *reg,
                              uint32_t name, const char *itf,
                              uint32_t version) {
  cout << "[Roundtrip] name: " << name << ", interface: '" << itf
       << "', version: " << version << endl;
  struct state *state = static_cast<struct state *>(data);
  if (strcmp(itf, wl_compositor_interface.name) == 0) {
    cout << "[Log] Binding compositor..." << endl;
    state->compositor = static_cast<struct wl_compositor *>(
        wl_registry_bind(reg, name, &wl_compositor_interface, version));
  } else if (strcmp(itf, wl_shm_interface.name) == 0) {
    cout << "[Log] Binding shm..." << endl;
    state->shm = static_cast<struct wl_shm *>(
        wl_registry_bind(reg, name, &wl_shm_interface, version));
  } else if (strcmp(itf, xdg_wm_base_interface.name) == 0) {
    cout << "[Log] Binding XDG WM..." << endl;
    state->xdg_wm_base = static_cast<struct xdg_wm_base *>(
        wl_registry_bind(reg, name, &xdg_wm_base_interface, version));
    xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
  } else if (strcmp(itf, wl_seat_interface.name) == 0) {
    cout << "[Log] Binding Seats..." << endl;
    state->seat = static_cast<struct wl_seat *>(
        wl_registry_bind(reg, name, &wl_seat_interface, version));
    auto pointer = wl_seat_get_pointer(state->seat);
    wl_pointer_add_listener(pointer, &pointer_listener, state);
  }
}

const struct wl_registry_listener registry_listener = {
    .global = &reg_handle_global,
    .global_remove = [](void *data, struct wl_registry *reg, uint32_t name) {}};

// Main

int main(int argc, char **argv) {
  gensym_count = 1;

  struct wl_display *display = wl_display_connect(NULL);
  if (!display) {
    cout << "[ERROR] Cannot connect to wayland display, exiting..." << endl;
    return 1;
  }
  struct wl_registry *reg = wl_display_get_registry(display);
  struct state state = {.registry = reg};

  wl_registry_add_listener(reg, &registry_listener, &state);

  cout << "[Log] Roundtrip..." << endl;
  wl_display_roundtrip(display);

  cout << "[Log] Creating surface..." << endl;
  state.surface = wl_compositor_create_surface(state.compositor);

  cout << "[Log] Creating XDG surface..." << endl;
  state.xdg_surface =
      xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
  xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);

  cout << "[Log] Setting XDG toplevel..." << endl;
  state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
  xdg_toplevel_set_title(state.xdg_toplevel, "WL Playground");

  cout << "[Log] Committing surface..." << endl;
  wl_surface_commit(state.surface);

  auto callback = wl_surface_frame(state.surface);
  wl_callback_add_listener(callback, &frame_listener, &state);

  while (wl_display_dispatch(display)) {
    cout << "Message dispatched." << endl;
    if (state.closed) {
      wl_display_disconnect(display);
      return 0;
    }
  }
  return 0;
}
