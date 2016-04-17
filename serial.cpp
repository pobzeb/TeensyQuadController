// http://ulisse.elettra.trieste.it/services/doc/serial/config.html

// bluefish /usr/include/termios.h /usr/include/bits/termios.h

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "serial.h"

unsigned char read_byte(sd_t *sd)
{
	int len = 0;
	unsigned char byte;
	
	while(len <= 0) {
		len = read(sd->fd, &byte, 1);
		if(len < 0) {
			if(errno == EAGAIN) continue;
			else { fprintf(stderr, "read_byte() error: %s(%d)\n", strerror(errno), errno); exit(-1); }
		}
	}
	
	return byte;
	
	//printf("0x%2X\n", byte);
}

int write_byte(sd_t *sd, unsigned char *byte)
{
	return write(sd->fd, byte, 1);
}

void read_data(sd_t *sd)
{
	unsigned char buf[255];
	int len = 0, i;
	//int retval, data_amt;
	//term_layout_t *term = (term_layout_t *)user_data;
	
	memset(&buf[0], 0, 255);
	
	//retval = ioctl(term->fd, FIONREAD, &data_amt);
	len = read(sd->fd, buf, 255);
	while(len > 0)
	{
		//handle the data
		//handle_recv_data(term, &buf[0], len);
		printf("%s\n", buf);
		for(i=0; i<len; i++) printf("0x%2X\n", buf[i]);
		
		//retval = ioctl(term->fd, FIONREAD, &data_amt);
		//try another read...if all is setup well, then read will return 0 bytes immediatly
		len = read(sd->fd, buf, 255);
	}
}

int write_data(sd_t *sd, unsigned char *data, unsigned int data_len)
{
	if(data_len == 0)	return 0;
	
/*
	int n = write(term->fd, data, data_len);
	if(n < 0)
	{
		//write failed, connection most likely dropped
		
	}
	
	return n;
*/
	return write(sd->fd, data, data_len);
}

int sdopen(sd_t *sd, char *dev)
{
	
	
/*
	The O_NOCTTY flag tells UNIX that this program doesn't want to be the "controlling terminal" for that port.
	If you don't specify this then any input (such as keyboard abort signals and so forth) will affect your process.
	Programs like getty(1M/8) use this feature when starting the login process,
	but normally a user program does not want this behavior.

	The O_NDELAY flag tells UNIX that this program doesn't care what state the DCD signal line is in
		- whether the other end of the port is up and running.
	If you do not specify this flag, your process will be put to sleep until the DCD signal line is the space voltage.
*/

	strncpy(sd->chardev, dev, sizeof(sd->chardev));
	sd->fd = open(sd->chardev, O_RDWR | O_NOCTTY | O_NDELAY);
	
	if (sd->fd < 0) return -1;	//Could not open the port.
	
	fcntl(sd->fd, F_SETFL, FNDELAY);	//set non-blocking

/*
	Reading data from a port is a little trickier.
	When you operate the port in raw data mode, each read(2) system call will return
	the number of characters that are actually available in the serial input buffers.
	If no characters are available, the call will block (wait) until characters come in,
	an interval timer expires, or an error occurs.
	The read function can be made to return immediately by doing the following:

	fcntl(fd, F_SETFL, FNDELAY);

	The FNDELAY option causes the read function to return 0 if no characters are available on the port.
	To restore normal (blocking) behavior, call fcntl() without the FNDELAY option:

    fcntl(fd, F_SETFL, 0);

	This is also used after opening a serial port with the O_NDELAY option.
*/
	
	return 0;
}

int sdconf(sd_t *sd)
{
	int err;
	
	//get the current options
	err = tcgetattr(sd->fd, &sd->saved);
	if(err) return err;
	
	memcpy(&sd->saved, &sd->settings, sizeof(struct termios));
	
	sd->settings.c_cflag |= (CLOCAL | CREAD);
	sd->settings.c_cflag |= B115200;
	sd->settings.c_cflag |= CS8;
	sd->settings.c_cflag |= PARENB;
	
	//input flags
	//termios_p.c_iflag = IGNPAR | IGNBRK;
	
	//local options
	sd->settings.c_lflag     &= ~(ICANON | ECHO | ECHOE | ISIG);	//sets up raw input, NON-canonical
	
	//output options
	sd->settings.c_oflag     &= ~OPOST;		//disable post-processing, to use raw output
	
	sd->settings.c_cc[VMIN]  = 1;
	sd->settings.c_cc[VTIME] = 0;
	
	//set the options
	err = tcsetattr(sd->fd, TCSANOW, &sd->settings);
	if(err) return err;
	
	//flush the buffers
	tcflush(sd->fd, TCOFLUSH);
	tcflush(sd->fd, TCIFLUSH);
	
	return 0;
}

#if 0
int configure_device(term_layout_t *term)
{
	struct termios settings;
	
	memcpy(&settings, &settings, sizeof(struct termios));
	
	//get the current options
	//tcgetattr(term->fd, &settings);
	
	//control options
		//taken from gtkterm
	settings.c_cflag |= CREAD;		//
	
	//load speed
	switch(term->speed_option)
	{
		case SPEED_300_OPT:		settings.c_cflag |= B300;		break;
		case SPEED_600_OPT:		settings.c_cflag |= B600;		break;
		case SPEED_1200_OPT:	settings.c_cflag |= B1200;		break;
		case SPEED_2400_OPT:	settings.c_cflag |= B2400;		break;
		case SPEED_4800_OPT:	settings.c_cflag |= B4800;		break;
		case SPEED_9600_OPT:	settings.c_cflag |= B9600;		break;
		case SPEED_19200_OPT:	settings.c_cflag |= B19200;		break;
		case SPEED_38400_OPT:	settings.c_cflag |= B38400;		break;
		case SPEED_57600_OPT:	settings.c_cflag |= B57600;		break;
		case SPEED_115200_OPT:	settings.c_cflag |= B115200;	break;
	}
	
	//load bits
	switch(term->bits_option)
	{
		case BITS_FIVE_OPT:		settings.c_cflag |= CS5;	break;
		case BITS_SIX_OPT:		settings.c_cflag |= CS6;	break;
		case BITS_SEVEN_OPT:	settings.c_cflag |= CS7;	break;
		case BITS_EIGHT_OPT:	settings.c_cflag |= CS8;	break;
	}

	//load parity
	switch(term->parity_option)
	{
		case PARITY_EVEN_OPT:
			settings.c_cflag |= PARENB;
			break;
		case PARITY_ODD_OPT:
			settings.c_cflag |= (PARENB | PARODD);
			break;
	}
	
	//load flowctl
	switch(term->flowctl_option)
	{
		case FLOWCTL_NONE_OPT:
			settings.c_cflag |= CLOCAL;			//NO flowctl
			break;
		case FLOWCTL_RTSCTS_OPT:
			settings.c_cflag |= CRTSCTS;		//hardware flowctl, Also called CNEW_RTSCTS
			break;
		case FLOWCTL_XONXOFF_OPT:
			settings.c_iflag |= (IXON | IXOFF | IXANY);		//software flow control
			break;
	}
	
	
	//input flags
	//termios_p.c_iflag = IGNPAR | IGNBRK;
	
	//local options
	settings.c_lflag     &= ~(ICANON | ECHO | ECHOE | ISIG);	//sets up raw input, NON-canonical
	
	//output options
	settings.c_oflag     &= ~OPOST;		//disable post-processing, to use raw output
	
	
	settings.c_cc[VMIN]  = 1;
	settings.c_cc[VTIME] = 0;
	
	//set the options
	tcsetattr(term->fd, TCSANOW, &settings);
	
	//flush the buffers
	tcflush(term->fd, TCOFLUSH);  
	tcflush(term->fd, TCIFLUSH);
	
	term->fd_io_id = gtk_input_add_full(term->fd, GDK_INPUT_READ, (GdkInputFunction)read_data, NULL, term, NULL);
	
	return 0;
}
#endif

void sdclose(sd_t *sd)
{
	//reset to original settings?
	
	close(sd->fd);
}

#if 0
int speed_opt_to_speed(int speed_opt)
{
	int speed = -1;
	
	switch(speed_opt)
	{
		case SPEED_300_OPT:		speed = 300;	break;
		case SPEED_600_OPT:		speed = 600;	break;
		case SPEED_1200_OPT:	speed = 1200;	break;
		case SPEED_2400_OPT:	speed = 2400;	break;
		case SPEED_4800_OPT:	speed = 4800;	break;
		case SPEED_9600_OPT:	speed = 9600;	break;
		case SPEED_19200_OPT:	speed = 19200;	break;
		case SPEED_38400_OPT:	speed = 38400;	break;
		case SPEED_57600_OPT:	speed = 57600;	break;
		case SPEED_115200_OPT:	speed = 115200;	break;
	}
	
	return speed;
}

int bits_opt_to_bits(int bits_opt)
{
	int bits = -1;
	
	switch(bits_opt)
	{
		case BITS_FIVE_OPT:		bits = 5;	break;
		case BITS_SIX_OPT:		bits = 6;	break;
		case BITS_SEVEN_OPT:	bits = 7;	break;
		case BITS_EIGHT_OPT:	bits = 8;	break;
	}
	
	return bits;
}

char parity_opt_to_parity(int parity_opt)
{
	char parity = 'N';
	
	switch(parity_opt)
	{
		case PARITY_EVEN_OPT:
			parity = 'E';
			break;
		case PARITY_ODD_OPT:
			parity = 'O';
			break;
	}
	
	return parity;
}

int stopbits_opt_to_stopbits(int stopbits_opt)
{
	int stopbits = -1;
	
	switch(stopbits_opt)
	{
		case STOPBITS_ONE_OPT:		stopbits = 1;	break;
		case STOPBITS_TWO_OPT:		stopbits = 2;	break;
	}
	
	return stopbits;
}
#endif
