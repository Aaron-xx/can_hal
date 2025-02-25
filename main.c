#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "can_hal.h"


int main(void)
{
	canhal_ctx ctx;
	if (!canhal_init(&ctx, "/dev/spidev0.0"))
	{
		return -1;
	}
	int sock = canhal_get_read_fd(ctx);
	uint8_t buf[256];
	while (1)
	{
		int n = recv(sock, buf, sizeof(buf), 0);
		if (n > 0)
		{
			printf("Received %d bytes: ", n);
			for (int i = 0; i < n; i++)
			{
				printf("%02X ", buf[i]);
			}
			printf("\n");
		}
	}
	canhal_close(ctx);
	return 0;
}
