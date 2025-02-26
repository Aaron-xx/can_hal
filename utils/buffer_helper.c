#include <assert.h>
#include "buffer_helper.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <alloca.h>
#include <stdlib.h>
#include <string.h>

struct buffer_helper
{
    char dbg_name[32];
    uint8_t frame_head[2];
    uint8_t frame_head_size;
    uint32_t frame_size; // frame_size 是一个 frame 的长度， 必须小于 buff 数组的大小
    uint32_t payload_size;
    uint32_t already_read;

    // 如果 payload_cb 为null，直接调用 cb。如果payload_cb不为空，调用payload_cb。
    buffer_helper_callback cb;
    void *cb_userdata;

    uint32_t receive_frame_count;

    // 接收到一个完整的 frame 以后，先调用 payload_callback 看是否还有后继的数据需要读。如果没有了，就调用 cb
    buffer_helper_payload_callback payload_cb;

    char buff[2048]; // 必须大于 485/can 协议里最大长度的 frame
    ring_buffer_t ring_buffer;
};

struct buffer_helper *buffer_helper_new(struct buffer_helper_meta *bhmeta, buffer_helper_callback cb, void *userdata)
{
    const uint8_t *frame_head = bhmeta->frame_head;
    int frame_head_size = bhmeta->frame_head_size;
    int frame_size = bhmeta->frame_size;
    
    assert(frame_head_size <= STRUCT_MEMBER_SIZE(struct buffer_helper, frame_head));
    assert(frame_size <= STRUCT_MEMBER_SIZE(struct buffer_helper, buff));

    struct buffer_helper *bh = calloc(1, sizeof(struct buffer_helper));
    bh->already_read = 0;
    bh->frame_head_size = frame_head_size;
    bh->frame_size = frame_size;
    bh->cb = cb;
    bh->cb_userdata = userdata;
    ring_buffer_init(&bh->ring_buffer, bh->buff, STRUCT_MEMBER_SIZE(struct buffer_helper, buff));
    memcpy(bh->frame_head, frame_head, frame_head_size);

    return bh;
}

void buffer_helper_reset(struct buffer_helper *bh)
{
    bh->already_read = 0;
    bh->payload_size = 0;
}

/*
    返回 true： 表示读完了一个完整的 frame， 大小为  bh->frame_size
    返回 false： 如果 error ！= 0 ，表示包头错了
*/
bool buffer_helper_recv(struct buffer_helper *bh, int *error)
{
    int read_size = 0;
    assert(bh != NULL);

    if (bh->already_read < (bh->frame_size + bh->payload_size))
    {
        size_t to_read;

        while ((bh->frame_size + bh->payload_size) > bh->already_read)
        {
            int all_size = bh->frame_size + bh->payload_size;

            // 读包头期间，需要一个字节地去读
            bool read_head = bh->already_read < bh->frame_head_size;
            if (read_head)
                to_read = 1;
            else
                to_read = all_size - bh->already_read;

            int total_bytes_in_rb = ring_buffer_num_items(&bh->ring_buffer) - bh->already_read;
            if (total_bytes_in_rb >= to_read)
            {
                // do nothing
                read_size = to_read;
            }
            else if (total_bytes_in_rb > 0 && total_bytes_in_rb < to_read)
            {
                read_size = total_bytes_in_rb;
            }
            else
            {
                read_size = 0;
                return false;
            }

            bh->already_read += read_size;

            // 需要检查包头是否正确
            if (read_head)
            {
                // head wrong
                bool flag = false;
                for (int n = 0; n < bh->already_read; n++)
                {
                    char data;
                    if (!ring_buffer_peek(&bh->ring_buffer, &data, n) || memcmp(&data, bh->frame_head + n, 1) != 0)
                    {
                        flag = true;
                        break;
                    }
                }
                if (flag)
                {
                    *error = -1;
                    return false;
                }
            }

            if (bh->already_read == bh->frame_size)
            {
                int continue_to_read;
                if (bh->payload_cb != NULL && (continue_to_read = bh->payload_cb(bh)) > 0)
                {
                    bh->payload_size = continue_to_read;
                }
            }
        }
    }

    return bh->already_read >= (bh->frame_size + bh->payload_size);
}

void buffer_helper_loop(struct buffer_helper *bh, char *buff, int buff_len)
{
    int error = 0;
    // printf("\nbuff_len = %d\n", buff_len);
    ring_buffer_queue_arr(&bh->ring_buffer, buff, buff_len);
    while (1)
    {
        error = 0;
        if (buffer_helper_recv(bh, &error))
        {
            char *data = alloca(bh->frame_size + bh->payload_size);
            ring_buffer_dequeue_arr(&bh->ring_buffer, data, bh->frame_size + bh->payload_size);
            // deal package
            if (bh->cb != NULL) {
                (bh->cb)((uint8_t *)data, bh->frame_size + bh->payload_size, bh->cb_userdata);
                bh->receive_frame_count++;
            }
                
            // dump_memory(data, global_bh.frame_size);
            buffer_helper_reset(bh);
        }
        else
        {
            if (error != 0)
            {
                char data;
                // 读包头期间，是1个字节1个字节读的， 所以这里只需要 dequeue 1个字节
                ring_buffer_dequeue(&bh->ring_buffer, &data);
                printf("head is wrong, current mod name=%s, c=0x%02x\n", buffer_helper_get_name(bh), (uint8_t)data);
                buffer_helper_reset(bh); // 设置 alread_read = 0;
            }
            else
            {
                break;
            }
        }
    }
}

void buffer_helper_clear_receive_count(struct buffer_helper *bh)
{
    bh->receive_frame_count = 0;
}

uint32_t buffer_helper_get_receive_count(struct buffer_helper *bh)
{
    return bh->receive_frame_count;
}

void buffer_helper_set_name(struct buffer_helper *bh, const char *name)
{
    snprintf(bh->dbg_name, STRUCT_MEMBER_SIZE(struct buffer_helper, dbg_name), "%s", name);
}

char *buffer_helper_get_name(struct buffer_helper *bh)
{
    return bh->dbg_name;
}

void buffer_helper_set_payload_callback(struct buffer_helper *bh, buffer_helper_payload_callback cb)
{
    bh->payload_cb = cb;
}

ring_buffer_t *buffer_helper_get_ringbuffer(struct buffer_helper *bh)
{
    return &bh->ring_buffer;
}

uint32_t buffer_helper_get_already_read_bytes(struct buffer_helper *bh)
{
    return bh->already_read;
}

void *buffer_helper_get_userdata(struct buffer_helper*bh)
{
    return bh->cb_userdata;
}

void buffer_helper_update_meta(struct buffer_helper*bh, struct buffer_helper_meta *bhmeta)
{
    //bh->frame_head = bhmeta->frame_head;
    bh->frame_head_size = bhmeta->frame_head_size;
    bh->frame_size = bhmeta->frame_size;
    return;
}