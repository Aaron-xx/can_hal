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
#include "utils/ringbuffer.h"
#include "utils/buffer_helper.h"
#include "spidev.h"

#define CAN_CACHE_SIZE (1024 * 1024)
#define CAN_SPI_MAX_CHANNEL (1)

static struct
{
	int spi_fd;			  /**< SPI 设备文件描述符 */
	int sock_fd;		  /**< Unix domain socket */
	pthread_t thread;	  /**< SPI 读取线程 */
	ring_buffer_t gl_can_send_ring; /**< SPI 发送环形缓冲区 */
	volatile int running; /**< 线程运行标志 */
} g_can_ctx = {
	.spi_fd = -1,
	.sock_fd = -1,
	.running = 0};

static struct
{
	uint32_t can_id;
	bool is_extended;
	uint32_t mask;
	drv_can_filter_callback callback;
	void *context;
} global_can_filter[128];

struct spi_can_frame
{
	uint8_t head;
	uint8_t dlc : 4;
	uint8_t rtr : 2;	  // 1 is remote frame, 0 is data frame
	uint8_t ide : 1;	  // 1 is extended, 0 is standard frame
	uint8_t spi_addr : 1; // choose spi addr
	uint32_t can_id;
	uint8_t payload[8];
	uint8_t tail;
	uint8_t xor_verify;
} __attribute__((packed));

#define HEAD_SIGN (0x7e)
#define TAIL_SIGN (0x7d)

static bool driver_can_find_filter(uint32_t can_id, drv_can_filter_callback *cb, void **context);

#define CAN_FRAME_LENGTH (sizeof(struct spi_can_frame))
static struct buffer_helper *global_bh[CAN_SPI_MAX_CHANNEL] = {NULL};

#define THIS_SPI_ADDR 1

#define CAN_FRAME_HEAD_LENGTH (1)

static const char *device = "/dev/spidev0.0";
// static uint8_t mode = SPI_MODE_3 | SPI_LSB_FIRST; /* SPI 通信使用全双工，设置 CPOL＝0，CPHA＝0。 */
static uint8_t mode = SPI_CPOL | SPI_CPHA; /* SPI 通信使用全双工，设置 CPOL＝0，CPHA＝0。 */

static uint8_t bits = 8; /* ８ｂiｔｓ读写，MSB first。*/
// static uint32_t speed = 562500; //12*1000*1000;/* 设置传输速度 */
static uint32_t speed = 1125000; // 12*1000*1000;/* 设置传输速度 */

static uint16_t delay = 0;

char can_send_buf[CAN_CACHE_SIZE];

static int SPI_Transfer(const uint8_t *TxBuf, uint8_t *RxBuf, int len)
{
	int ret;
	int fd = g_can_ctx.spi_fd;
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)TxBuf,
		.rx_buf = (unsigned long)RxBuf,
		.len = len,
		.delay_usecs = delay,
	};
	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
		perror("can't send spi message\n");
	else
	{
#if 0
        int i;
        printf("send spi message Succeed ret=%d\r\n",ret );
        printf("SPI Send [Len:%d]: \r\n", len);
        for (i = 0; i < len; i++)
        {
            if (i % 8 == 0)
                printf("\r\n");
            printf("0x%02X \r\n", TxBuf[i]);
        }
        printf("\n");
        printf("SPI Receive [len:%d]:\n", len);
        for (i = 0; i < len; i++)
        {
            if (i % 8 == 0)
                printf("\r\n");
            printf("0x%02X \r\n", RxBuf[i]);
        }
        printf("\n");
#endif
	}
	return ret;
}

static uint8_t xor_calculate(struct spi_can_frame *frame)
{
	uint8_t *p = (uint8_t *)frame;
	uint8_t xor = 0;
	for (int n = 0; n < (sizeof(struct spi_can_frame) - 1); n++)
	{
		xor ^= p[n];
	}
	return xor;
}

static bool xor_verify_ok(struct spi_can_frame *frame)
{
	uint8_t *p = (uint8_t *)frame;
	uint8_t xor = 0;

	if (frame->head != HEAD_SIGN || frame->tail != TAIL_SIGN)
	{
		// blog_dbg("head tail error\n");
		// show_data_with_msg("raw fram:", frame, sizeof(*frame));

		return false;
	}

	xor = xor_calculate(frame);
	// show_data_with_msg("raw fram:", frame, sizeof(*frame));
	// blog_dbg("xor = %d\n", frame->xor_verify);
	return frame->xor_verify == xor;
}

static void *can_hal_thread(void *arg)
{
	(void)arg;
	static struct spi_can_frame rx_frame;
	static struct spi_can_frame tx_frame;

	while (g_can_ctx.running)
	{
		// 一直处理spi ，直到没有数据才退出
		while (1)
		{
			int ret;
			bool has_new_spi_frame = false;
			bzero(&rx_frame, CAN_FRAME_LENGTH);

			if (ring_buffer_num_items(&g_can_ctx.gl_can_send_ring) >= CAN_FRAME_LENGTH)
			{
				// 如果有数据 需要写入 spi ， 那么就从 ringbuffer里读出来，放入 tx-frame
				ring_buffer_dequeue_arr(&g_can_ctx.gl_can_send_ring, (char *)&tx_frame, CAN_FRAME_LENGTH);
				// printf("send can id=0x%08x, dlc=%d\n", tx_frame.can_id, tx_frame.dlc);
				// show_data_with_msg("send can", &tx_frame, sizeof(tx_frame));
			}
			else
			{
				tx_frame.head = 0xff;
				tx_frame.spi_addr = THIS_SPI_ADDR;
			}
#if 1
			ret = SPI_Transfer((const uint8_t *)&tx_frame, (uint8_t *)&rx_frame, CAN_FRAME_LENGTH);

			if (ret > 0)
			{
				int v = xor_verify_ok(&rx_frame);
				if (v && rx_frame.ide && rx_frame.rtr == 0)
				{
					printf("can id=0x%04x dlc=%d payload=:\r\n", rx_frame.can_id, rx_frame.dlc);
					// fprintf(stderr, "can id=0x%04x dlc=%d payload=:\r\n", rx_frame.can_id, rx_frame.dlc);
					// show_data_with_msg("spi can payload=", &rx_frame, sizeof(rx_frame));
					if (rx_frame.spi_addr < CAN_SPI_MAX_CHANNEL)
					{
						buffer_helper_loop(global_bh[rx_frame.spi_addr], (char *)&rx_frame, sizeof(rx_frame));
						has_new_spi_frame = true;
					}
					else
					{
						printf("spi error 1\n");
					}
				}
				else
				{
					printf("sssstep, %d, %d, %d\n", v,rx_frame.ide, rx_frame.rtr);
					// show_data_with_msg("fuck", &rx_frame, CAN_FRAME_LENGTH);
				}
			}
			else
			{
				printf("spi error 2\n");
			}
#endif

			if (ring_buffer_num_items(&g_can_ctx.gl_can_send_ring) < CAN_FRAME_LENGTH &&
				has_new_spi_frame == false)
			{
				// printf("break spi\n");
				break;
			}

		} // end of while(1) for loop read spi

		usleep(10000);
	}
	return NULL;
}

static bool driver_can_spi_send_channel(uint8_t can_channel, struct can_frame *frame)
{
    // if ( can_channel >= CAN_SPI_MAX_CHANNEL)
    //     return false;

    struct spi_can_frame spi_frame;
    spi_frame.head = HEAD_SIGN;
    spi_frame.tail = TAIL_SIGN;
    spi_frame.dlc = frame->can_dlc;
    spi_frame.can_id = frame->can_id;
    memcpy(spi_frame.payload, frame->payload, frame->can_dlc);
    spi_frame.rtr = frame->rtr;
    spi_frame.ide = frame->extended_id;
    spi_frame.spi_addr = can_channel;

    spi_frame.xor_verify = xor_calculate(&spi_frame);

    ring_buffer_safe_queue_arr(
        &g_can_ctx.gl_can_send_ring, 
        (const char*)&spi_frame, 
        sizeof(struct spi_can_frame)
    );
    return true;
}

static bool driver_can_find_filter(uint32_t can_id, drv_can_filter_callback *cb, void **context)
{
	for (int n = 0; n < ARRAY_MEMBER_COUNT(global_can_filter); n++)
	{
		if ((can_id & global_can_filter[n].mask) == global_can_filter[n].can_id)
		{
			*cb = global_can_filter[n].callback;
			*context = global_can_filter[n].context;
			return true;
		}
	}
	return false;
}

static void spican_frame_callback(uint8_t *can_raw_data, int len, void *useless)
{
	struct spi_can_frame *frame = (struct spi_can_frame *)can_raw_data;

	drv_can_filter_callback callback;
	void *context;
	if (driver_can_find_filter(frame->can_id, &callback, &context))
	{
		if (callback != NULL)
		{

			struct can_frame can;
			can.can_dlc = frame->dlc;
			can.can_id = frame->can_id;
			can.extended_id = frame->ide;
			can.rtr = frame->rtr;
			memcpy(can.payload, frame->payload, 8);

			callback(context, &can);
		}
	}
	else
	{
		printf("unknown can id=0x%08x\n", frame->can_id);
	}
}

static int SPI_Open(void)
{
	int fd = g_can_ctx.spi_fd;
	int ret = 0;
	if (fd >= 0) /* 设备已打开 */
		return 0xF1;
	fd = open(device, O_RDWR);
	if (fd < 0)
		printf("can't open device\n");
	else
		printf("SPI - Open Succeed. Start Init SPI...\n");
	g_can_ctx.spi_fd = fd;

	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		printf("can't set spi mode\n");
	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		printf("can't get spi mode\n");

	/*
	 * bits per word
	 */
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		printf("can't set bits per word\n");
	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		printf("can't get bits per word\n");
	/*
	 * max speed hz
	 */
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		printf("can't set max speed hz\n");
	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		printf("can't get max speed hz\n");

	// printf("spi mode: %d\n", mode);
	printf("bits per word: %d\n", bits);
	printf("max speed: %d KHz (%d MHz)\n", speed / 1000, speed / 1000 / 1000);
	return ret;
}

static bool init_drv_can_spi(const char *dev)
{
	uint8_t frame_head[CAN_FRAME_HEAD_LENGTH] = {0x7e};

	int ret = SPI_Open();
	if (ret == 0)
	{
		// TODO 根据协议设置
		struct buffer_helper_meta buffer_meta = {
			.frame_head = frame_head,
			.frame_head_size = CAN_FRAME_HEAD_LENGTH,
			.frame_size = CAN_FRAME_LENGTH,
		};

		global_bh[0] = buffer_helper_new(&buffer_meta, spican_frame_callback, NULL);
		buffer_helper_set_name(global_bh[0], "spi_can");

		ring_buffer_init(&g_can_ctx.gl_can_send_ring, can_send_buf, CAN_CACHE_SIZE);

		return true;
	}
	else
		return false;
}

bool canhal_init(canhal_ctx *context, const char *device)
{
	init_drv_can_spi(device);

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
		if (driver_can_spi_send_channel(0, data) != 1)
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