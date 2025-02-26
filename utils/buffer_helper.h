#ifndef __UTILS_H__
#define __UTILS_H__

#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>
#include <assert.h>
#include "ringbuffer.h"

#define POLY (0x1021)
#define INIT (0x0000)

#define STRUCT_MEMBER_SIZE(name, field) \
  (sizeof(((name *)0)->field))

#define ARRAY_MEMBER_COUNT(arr)  sizeof(arr)/sizeof(arr[0])

struct buffer_helper;

struct buffer_helper_meta {
  const uint8_t *frame_head;
  int frame_head_size;
  int frame_size;
};

typedef void (*buffer_helper_callback)(uint8_t *buff, int len, void *userdata);

//如果是长度不固定的包，读完包头以后，还要根据包头读取剩余的payload
typedef int (*buffer_helper_payload_callback)(struct buffer_helper *this);

struct buffer_helper *buffer_helper_new(struct buffer_helper_meta *bmeta, buffer_helper_callback cb, void *userdata);
void buffer_helper_set_payload_callback(struct buffer_helper *bh, buffer_helper_payload_callback cb);
ring_buffer_t *buffer_helper_get_ringbuffer(struct buffer_helper *bh);
void buffer_helper_reset(struct buffer_helper *bh);
void buffer_helper_loop(struct buffer_helper *bh, char *buff, int buff_len);
void buffer_helper_set_name(struct buffer_helper *bh, const char *name);
char *buffer_helper_get_name(struct buffer_helper *bh);
void buffer_helper_clear_receive_count(struct buffer_helper *bh);
uint32_t buffer_helper_get_receive_count(struct buffer_helper *bh);
uint32_t buffer_helper_get_already_read_bytes(struct buffer_helper *bh);
void *buffer_helper_get_userdata(struct buffer_helper*bh);
void buffer_helper_update_meta(struct buffer_helper*bh, struct buffer_helper_meta *bhmeta);

#endif
