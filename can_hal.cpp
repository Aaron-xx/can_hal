#include "can_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include "pt/pt.h"

#define SPI_BUFFER_SIZE 256

static struct
{
	int spi_fd;		  /**< SPI 设备文件描述符 */
	int sock_fd;	  /**< Unix domain socket */
	struct pt pt;	  /**< Protothread 协程 */
	pthread_t thread; /**< SPI 读取线程 */
	int running;	  /**< 线程运行标志 */
} g_can_ctx = {
	.spi_fd = -1,
	.sock_fd = -1,
	.running = 0};

static uint8_t spi_buffer[SPI_BUFFER_SIZE];

static PT_THREAD(can_hal_pt(struct pt *pt))
{
	int n = 0;
	PT_BEGIN(pt);

	while (1)
	{
		n = read(g_can_ctx.spi_fd, spi_buffer, sizeof(spi_buffer));
		if (n > 0)
		{
			if (write(g_can_ctx.sock_fd, spi_buffer, n) < 0)
			{
				perror("write(sock_fd) error");
			}
		}
		else if (n == 0)
		{
			/* SPI 设备关闭 */
			break;
		}
		else
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				PT_YIELD(pt);
			}
			else
			{
				perror("read(spi) error");
				break;
			}
		}
		PT_YIELD(pt);
	}

	PT_END(pt);
}

static void *can_hal_thread(void *arg)
{
	(void)arg;
	while (g_can_ctx.running)
	{
		can_hal_pt(&g_can_ctx.pt);
		usleep(10000); // 降低 CPU 占用
	}
	return NULL;
}

bool canhal_init(canhal_ctx *context, const char *device)
{
	g_can_ctx.spi_fd = open(device, O_RDWR | O_NONBLOCK);
	if (g_can_ctx.spi_fd < 0)
	{
		perror("open SPI error");
		return false;
	}

	int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0)
	{
		perror("socket error");
		close(g_can_ctx.spi_fd);
		return false;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "\0can_hal", sizeof(addr.sun_path) - 1);
	size_t addr_len = sizeof(sa_family_t) + strlen("can_hal") + 1;

	if (bind(sock, (struct sockaddr *)&addr, addr_len) < 0)
	{
		perror("bind error");
		close(sock);
		close(g_can_ctx.spi_fd);
		return false;
	}

	if (connect(sock, (struct sockaddr *)&addr, addr_len) < 0)
	{
		perror("connect error");
		close(sock);
		close(g_can_ctx.spi_fd);
		return false;
	}
	g_can_ctx.sock_fd = sock;

	// 初始化协程
	PT_INIT(&g_can_ctx.pt);

	if (pthread_create(&g_can_ctx.thread, NULL, can_hal_thread, NULL) != 0)
	{
		perror("pthread_create error");
		close(sock);
		close(g_can_ctx.spi_fd);
		return false;
	}
	g_can_ctx.running = 1;

	if (context)
	{
		*context = (canhal_ctx)&g_can_ctx;
	}
	return true;
}

void canhal_close(canhal_ctx ctx)
{
	if (!ctx)
		return;
	g_can_ctx.running = 0;
	pthread_join(g_can_ctx.thread, NULL);
	if (g_can_ctx.spi_fd >= 0)
		close(g_can_ctx.spi_fd);
	if (g_can_ctx.sock_fd >= 0)
		close(g_can_ctx.sock_fd);
	g_can_ctx.spi_fd = -1;
	g_can_ctx.sock_fd = -1;
}

void canhal_write(canhal_ctx ctx, void *data, uint32_t data_len)
{
	if (!ctx)
		return;
	if (g_can_ctx.spi_fd >= 0)
	{
		if (write(g_can_ctx.spi_fd, data, data_len) < 0)
		{
			perror("write(spi) error");
		}
	}
}

int canhal_get_read_fd(canhal_ctx ctx)
{
	if (!ctx)
		return false;
	return g_can_ctx.sock_fd;
}
