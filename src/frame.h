#ifndef HTCW_FRAME
#define HTCW_FRAME
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
    FRAME_ERROR_ARG = -5,
    FRAME_ERROR_UNDERFLOW = -4,
    FRAME_ERROR_OVERFLOW = -3,
    FRAME_ERROR_CRC = -2,
    FRAME_EOF = -1,
    FRAME_SUCCESS   =  0
};
typedef int(*frame_read_callback_t)(void* state);
typedef int(*frame_write_callback_t)(uint8_t value, void* state);

typedef void* frame_handle_t;

frame_handle_t frame_create(size_t max_payload_size,
    frame_read_callback_t on_read_callback, void* on_read_callback_state,
    frame_write_callback_t on_write_callback, void* on_write_callback_state
);
void frame_destroy(frame_handle_t handle);
int frame_get(frame_handle_t handle, void** out_data, size_t* out_size);
int frame_discard(frame_handle_t handle);
int frame_put(frame_handle_t handle, uint8_t cmd, const void* payload, size_t size);
#ifdef __cplusplus
}
#endif
#endif // HTCW_FRAME