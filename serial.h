#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <termios.h>

typedef struct {
	int fd;
	char chardev[64];
	struct termios saved;
	struct termios settings;
} sd_t;

unsigned char read_byte(sd_t *sd);
int write_byte(sd_t *sd, unsigned char *byte);
void read_data(sd_t *sd);
int write_data(sd_t *sd, unsigned char *data, unsigned int data_len);
int sdopen(sd_t *sd, char *dev);
int sdconf(sd_t *sd);
void sdclose(sd_t *sd);

#define LF	(0x0A)
#define CR	(0x0D)

#define SPEED_300_OPT		(1)
#define SPEED_600_OPT		(2)
#define SPEED_1200_OPT		(3)
#define SPEED_2400_OPT		(4)
#define SPEED_4800_OPT		(5)
#define SPEED_9600_OPT		(6)
#define SPEED_19200_OPT		(7)
#define SPEED_38400_OPT		(8)
#define SPEED_57600_OPT		(9)
#define SPEED_115200_OPT	(10)

#define PARITY_NONE_OPT	(0)
#define PARITY_ODD_OPT	(1)
#define PARITY_EVEN_OPT	(2)

#define BITS_FIVE_OPT	(1)
#define BITS_SIX_OPT	(2)
#define BITS_SEVEN_OPT	(3)
#define BITS_EIGHT_OPT	(4)

#define STOPBITS_ONE_OPT	(1)
#define STOPBITS_TWO_OPT	(2)

#define FLOWCTL_NONE_OPT	(0)
#define FLOWCTL_RTSCTS_OPT	(1)
#define FLOWCTL_XONXOFF_OPT	(2)

#endif
