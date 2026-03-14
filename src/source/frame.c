#include "frame.h"
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#define FRAME_HEADER_LENGTH (8 + 4 + 4)
typedef struct {
    size_t payload_max_size;
    frame_read_callback_t read_cb;
    void* read_state;
    frame_write_callback_t write_cb;
    void* write_state;
    uint8_t* read_buffer;
    uint8_t start;
    uint8_t start_count;
} frame_t;

static uint32_t crc32(const uint8_t* data, size_t length, uint32_t seed) {
    uint32_t result = seed;
    while (length--) {
        result ^= *data++;
    }
    return result;
}
static uint8_t cmd_from_frame(const frame_t* frame) {
    const uint8_t* frame_data = frame->read_buffer;
    if(frame_data[0]<128) return 0;
    return (frame_data[0] == frame_data[1] && frame_data[0] == frame_data[2] && frame_data[0] == frame_data[3] &&
            frame_data[0] == frame_data[4] && frame_data[0] == frame_data[5] && frame_data[0] == frame_data[6] && frame_data[0] == frame_data[7])
               ? (int8_t)(frame_data[0] - 128)
               : 0;
}
static size_t size_from_frame(const frame_t* frame) {
    const uint8_t* frame_data = frame->read_buffer;
    uint32_t* result = (uint32_t*)(frame_data + 8);
    return (size_t)*result;
}
static size_t crc_from_frame(const frame_t* frame) {
    const uint8_t* frame_data = frame->read_buffer;
    uint32_t* result = (uint32_t*)(frame_data + 12);
    return (size_t)*result;
}
static int read_frame_marker(frame_t* frame) {
    int b = frame->read_cb(frame->read_state);
    if (b < 0) {
        return b;
    }
    if (frame->start_count == 0) {
        if (b < 128) {
            return FRAME_EOF;
        }
        frame->start = b;
        ++frame->start_count;
        frame->read_buffer[0] = b;
        return FRAME_EOF;
    }
    if (frame->start_count < 8) {
        if (frame->start != b) {
            frame->start_count = 0;
            frame->start = b;
            return FRAME_EOF;
        }
        frame->read_buffer[frame->start_count] = frame->start;
        ++frame->start_count;
        if (frame->start_count == 8) {
            frame->start_count = 0;
            return 0;
        }
        return FRAME_EOF;
    }
    frame->start_count = 0;
    return FRAME_EOF;
}
static int read_frame_header(frame_t* frame) {
    int res = read_frame_marker(frame);
    if (res < 0) {
        return res;
    }
    uint8_t* p = frame->read_buffer + 8;
    size_t remaining = 8;

    while (remaining--) {
        int b = frame->read_cb(frame->read_state);
        if (0 > b) {
            return FRAME_ERROR_UNDERFLOW;
        }
        *p++ = b;
    }
    return 0;
}
static int read_frame(frame_t* frame) {
    int res = read_frame_header(frame);
    if (res < 0) {
        return res;
    }
    if (cmd_from_frame(frame) == 0) return FRAME_EOF;
    size_t size = size_from_frame(frame);
    uint32_t crc = crc_from_frame(frame);
    uint8_t* p = frame->read_buffer + FRAME_HEADER_LENGTH;
    if (size > frame->payload_max_size) {
        return FRAME_ERROR_OVERFLOW;
    }
    size_t remaining = size;

    while (remaining--) {
        int b = frame->read_cb(frame->read_state);
        if (0 > b) {
            return FRAME_ERROR_UNDERFLOW;
        }
        *p++ = b;
    }
    if (crc != crc32(frame->read_buffer + FRAME_HEADER_LENGTH, size, UINT32_MAX / 3)) {
        return FRAME_ERROR_CRC;
    }
    return 0;
}

static int frame_update(frame_t* frame) {
    int res;
    res = read_frame(frame);
    if (-1 < res) {
        int cmd = cmd_from_frame(frame);
        if (cmd > 0) {
            uint32_t crc = crc32(frame->read_buffer + FRAME_HEADER_LENGTH, size_from_frame(frame), UINT32_MAX / 3);
            if (crc != crc_from_frame(frame)) {
                return FRAME_ERROR_CRC;
            } else {
                return 0;
            }
        }
    }
    return res;
}
int frame_get(frame_handle_t handle, void** out_data, size_t* out_size) {
    if (handle == NULL) {
        return FRAME_ERROR_ARG;
    }
    frame_t* frame = (frame_t*)handle;
    int res = cmd_from_frame(frame);
    if (res != 0) {
        memset(frame->read_buffer,0,FRAME_HEADER_LENGTH);
        return res;
    }
    res = frame_update(frame);
    if (res < 0) {
        return res;
    }
    res = cmd_from_frame(frame);
    if (res == 0) {
        return FRAME_EOF;
    }
    *out_data = frame->read_buffer + FRAME_HEADER_LENGTH;
    *out_size = size_from_frame(frame);
    memset(frame->read_buffer,0,FRAME_HEADER_LENGTH);
    return res;
}
int frame_discard(frame_handle_t handle) {
    if (handle == NULL) {
        return FRAME_ERROR_ARG;
    }
    frame_t* frame = (frame_t*)handle;
    memset(frame->read_buffer,0,FRAME_HEADER_LENGTH);
    return 0;
}
int frame_put(frame_handle_t handle, uint8_t cmd, const void* payload, size_t size) {
    if (handle == NULL) {
        return FRAME_ERROR_ARG;
    }
    if (cmd > 127) {
        return FRAME_ERROR_ARG;
    }
    int res;
    frame_t* frame = (frame_t*)handle;
    cmd += 128;
    for (int i = 0; i < 8; ++i) {
        res = frame->write_cb(cmd, frame->write_state);
        if (res < 0) return res;
    }
    res = frame->write_cb(size & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    res = frame->write_cb((size >> 8) & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    res = frame->write_cb((size >> 16) & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    res = frame->write_cb((size >> 24) & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    const uint8_t* p = (const uint8_t*)payload;
    int crc = crc32(p, size, UINT32_MAX / 3);
    res = frame->write_cb(crc & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    res = frame->write_cb((crc >> 8) & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    res = frame->write_cb((crc >> 16) & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    res = frame->write_cb((crc >> 24) & 0xFF, frame->write_state);
    if (res < 0) {
        return res;
    }
    while (size--) {
        res = frame->write_cb(*p++, frame->write_state);
        if (res < 0) {
            return res;
        }
    }
    return 0;
}
frame_handle_t frame_create(size_t max_payload_size,
                            frame_read_callback_t on_read_callback, void* on_read_callback_state,
                            frame_write_callback_t on_write_callback, void* on_write_callback_state) {
    uint8_t* rx_buffer = NULL;
    frame_t* result = NULL;
    if (on_read_callback == NULL || on_write_callback == NULL) {
        goto error;
    }
    rx_buffer = (uint8_t*)malloc(max_payload_size + FRAME_HEADER_LENGTH);
    if (rx_buffer == NULL) {
        goto error;
    }
    memset(rx_buffer,0,FRAME_HEADER_LENGTH);
    result = (frame_t*)malloc(sizeof(frame_t));
    if (result == NULL) {
        goto error;
    }
    memset(result, 0, sizeof(frame_t));
    result->read_buffer = rx_buffer;
    result->payload_max_size = max_payload_size;
    result->read_cb = on_read_callback;
    result->read_state = on_read_callback_state;
    result->write_cb = on_write_callback;
    result->write_state = on_write_callback_state;
    return result;
error:
    if (rx_buffer != NULL) {
        free(rx_buffer);
    }
    if (result != NULL) {
        free(result);
    }
    return NULL;
}
void frame_destroy(frame_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    frame_t* frame = (frame_t*)handle;
    if (frame->read_buffer != NULL) {
        free(frame->read_buffer);
        frame->read_buffer = NULL;
    }
    free(frame);
}