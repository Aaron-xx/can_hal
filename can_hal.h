#ifndef CAN_HAL_H
#define CAN_HAL_H

#include <stdbool.h>
#include <stdint.h>

struct can_frame
{
    uint32_t can_id;  /**<  can帧的id */
    uint32_t can_dlc; /**< can帧的负载长度*/
    bool extended_id; /**< 是否是扩展帧  */
    bool rtr; /**< 是否是遥控帧,大多数场景下都应该为false */
    uint8_t payload[8] __attribute__((aligned(8))) ;  /**<  can帧的数据部分 */
};

typedef void (*drv_can_filter_callback)(void*context, struct can_frame *can_frame);

typedef void *canhal_ctx;

bool canhal_init(canhal_ctx *ctx, const char *device_name);
bool canhal_is_open(canhal_ctx ctx);
void canhal_close(canhal_ctx ctx);
void canhal_write(canhal_ctx ctx, void *data, uint32_t data_len);
int canhal_get_read_fd(canhal_ctx ctx);


#endif
