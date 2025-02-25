#ifndef CAN_HAL_H
#define CAN_HAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *canhal_ctx;

bool canhal_init(canhal_ctx *ctx, const char *device_name);
bool canhal_is_open(canhal_ctx ctx);
void canhal_close(canhal_ctx ctx);
void canhal_write(canhal_ctx ctx, void *data, uint32_t data_len);
int canhal_get_read_fd(canhal_ctx ctx);


#ifdef __cplusplus
}
#endif

#endif
