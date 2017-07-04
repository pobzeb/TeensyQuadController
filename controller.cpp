#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <chrono>
#include <linux/joystick.h>
#include "serial.h"

// Define the radio tx interval
#define TX_DELTA 100

// Define the number of channels.
#define CHANNELS 6

// Min and max values of controller.
#define MIN_VALUE -32767
#define MAX_VALUE 32767

// Min and max output values.
#define MIN_OUT_VALUE 1000
#define MAX_OUT_VALUE 2000

#define THROTTLE_INPUT		 2
#define PITCH_INPUT		 1
#define ROLL_INPUT		 0
#define YAW_INPUT		 5
#define DIAL_INPUT 		 4
#define ARM_TOGGLE_INPUT	 3

using namespace std;

string controllerPort, radioPort;

sd_t radio;
int err;
int value;

int rollInput, rollOffset, pitchInput, pitchOffset, yawInput, yawOffset;

int joy_fd, num_of_axis = 0, num_of_buttons = 0, pid_mode = 0, pid_type = 0;
char name_of_joystick[80];
struct js_event jsBuffer[0xff];

// Create a ZMQ socket to publish.
zmq::context_t context(1);
zmq::socket_t socket(context, ZMQ_PUB);

// Message buffer and length.
char mbuf[1024];
int mlen = 0;

// Mapping function.
float map(int value, int s_low, int s_high, int d_low, int d_high) {
	return d_low + (d_high - d_low) * (value - s_low) / (s_high - s_low);
}

void sighandler(int sig) {
	// App killed.
	printf("Shutting Down...\n");
	close(joy_fd);
	sdclose(&radio);
	exit(0);
}

void publishMessage(char *buf, int len) {
	zmq::message_t message(len);
	memcpy(message.data(), buf, len);
	// printf("%s\n", buf);
	socket.send(message);
}

void processEvent(unsigned int channel[]) {
	// Send it over the radio.
	uint8_t numChannels = (uint8_t)CHANNELS;
	uint8_t h = (uint8_t)0x00;
	write(radio.fd, reinterpret_cast<const char *>(&h), 1);
	write(radio.fd, reinterpret_cast<const char *>(&h), 1);
	write(radio.fd, reinterpret_cast<const char *>(&numChannels), 1);
	for (int i = 0; i < numChannels; i++) {
		uint8_t b1 = (uint8_t)(channel[i] & 0xFF);
		uint8_t b2 = (uint8_t)((channel[i] >> 8) & 0xFF);
		write(radio.fd, reinterpret_cast<const char *>(&b1), 1);
		write(radio.fd, reinterpret_cast<const char *>(&b2), 1);
	}

	fflush(stdout);
}

void readRadioData() {
	int bytesAvailable = 0;
	int bufSize = 256, bufIdx = 0;
	unsigned char buf[bufSize];
	char rb = ' ';

	while (true) {
		bufIdx = 0;
		memset(&buf[0], 0, bufSize);
		bytesAvailable = read(radio.fd, buf, bufSize);
		if (bytesAvailable > 0) {
			// printf("%s", buf);
			publishMessage((char*)buf, bufSize);
		}
	}
}

void processJoystickEvents(int joystick_fd) {
	// Array to hold channel values.
	// [throttle, yaw, pitch, roll, dial, arm]
	unsigned int channel[CHANNELS];

	// Get the current time for the loop timer.
	auto lastMsgTime = chrono::steady_clock::now( );

	// Infinite loop.
	while (true) {
		// Read the joystick
		int events = read(joystick_fd, jsBuffer , sizeof(jsBuffer));
		if (events != -1) {
			events = events / sizeof(js_event);
			for (int idx = 0; idx < events; idx++) {
				// See what to do with the event
				switch (jsBuffer[idx].type & ~ JS_EVENT_INIT) {
					case JS_EVENT_AXIS: {
						// Map value to new min and max.
						value = map(jsBuffer[idx].value, MIN_VALUE, MAX_VALUE, MAX_OUT_VALUE, MIN_OUT_VALUE);

						if (jsBuffer[idx].number == THROTTLE_INPUT) {
							// Throttle
							channel[0] = value;
						}
						else if (jsBuffer[idx].number == PITCH_INPUT) {
							// Pitch
							channel[2] = value + pitchOffset;
						}
						else if (jsBuffer[idx].number == ROLL_INPUT) {
							// Roll
							channel[3] = value + rollOffset;
						}
						else if (jsBuffer[idx].number == YAW_INPUT) {
							// Yaw
							channel[1] = value + yawOffset;
						}

						// Map value to new min and max.
						value = map(jsBuffer[idx].value, MIN_VALUE, MAX_VALUE, MIN_OUT_VALUE, MAX_OUT_VALUE);

						if (jsBuffer[idx].number == DIAL_INPUT) {
							// Dial
							channel[4] = map(jsBuffer[idx].value, MIN_VALUE, MAX_VALUE, 0, 100);
						}
						else if (jsBuffer[idx].number == ARM_TOGGLE_INPUT) {
							channel[5] = jsBuffer[idx].value > 0 ? 2000 : 1000;
						}

						break;
					}
				}
			}
		}

		// Send a control message every 100ms.
		long long ms = chrono::duration_cast<chrono::milliseconds>( chrono::steady_clock::now( ) - lastMsgTime ).count();
		if (ms >= TX_DELTA) {
			lastMsgTime = chrono::steady_clock::now( );

			// Send the message.
			printf("CMD: %3lldms Trottle: %3d, Yaw: %3d, Pitch: %3d, Roll: %3d, Dial: %3d, Armed: %d\n", ms, channel[0], channel[1], channel[2], channel[3], channel[4], channel[5]);
			processEvent(channel);
		}

		if (errno != EAGAIN) {
			/* Error */
		}
	}
}

void doCalibration(int joystick_fd) {
	// Calibrate and get a baseline offset for roll, pitch and yaw.
	printf("Calibrating Joystick...\n");
	auto calibrationTimer = chrono::steady_clock::now();
	int calibrationSteps = 200;
	for (int calIdx = 0; calIdx < calibrationSteps; calIdx++) {
		// Read the joystick
		int events = read(joystick_fd, jsBuffer , sizeof(jsBuffer));
		if (events != -1) {
			events = events / sizeof(js_event);
			for (int idx = 0; idx < events; idx++) {
				// See what to do with the event
				switch (jsBuffer[idx].type & ~ JS_EVENT_INIT) {
					case JS_EVENT_AXIS: {
						// Map value to new min and max.
						value = map(jsBuffer[idx].value, MIN_VALUE, MAX_VALUE, MAX_OUT_VALUE, MIN_OUT_VALUE);
						if (jsBuffer[idx].number == YAW_INPUT) {
							// Yaw
							yawInput = value;
						}
						else if (jsBuffer[idx].number == PITCH_INPUT) {
							// Pitch
							pitchInput = value;
						}
						else if (jsBuffer[idx].number == ROLL_INPUT) {
							// Roll
							rollInput = value;
						}

						break;
					}
				}
			}
		}

		// Accumulate
		rollOffset  += rollInput;
		pitchOffset += pitchInput;
		yawOffset   += yawInput;

		while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - calibrationTimer).count() < 50);
		calibrationTimer = chrono::steady_clock::now();
	}

	// Get an average
	rollOffset  /= calibrationSteps;
	pitchOffset /= calibrationSteps;
	yawOffset   /= calibrationSteps;

	rollOffset = 1500 - rollOffset;
	pitchOffset = 1500 - pitchOffset;
	yawOffset = 1500 - yawOffset;
	printf("Calibration Complete. [Ro: %d, Po: %d, Yo: %d]\n", rollOffset, pitchOffset, yawOffset);
}

int main(int argc, char **argv) {
	if (argc < 3) {
		cerr << "Usage: " << argv[0] << " -c CONTROLLER_PORT -r RADIO_PORT\n"
			 << "Options:\n"
			 << "\t-c\tController port (Ex: /dev/input/js0)\n"
			 << "\t-r\tRadio port (Ex: /dev/ttyUSB0)\n\n"
			 << endl;
		return 1;
	}

	string arg;
	for (int idx = 1; idx < argc; ++idx) {
		arg = argv[idx];
		idx++;
		if (arg == "-c") {
			if (idx < argc) {
				controllerPort = argv[idx];
			}
		}
		else if (arg == "-r") {
			if (idx < argc) {
				radioPort = argv[idx];
			}
		}
	}

	// input of joystick values to variable joy_fd
	if ((joy_fd = open((char*)(controllerPort.c_str()), O_RDONLY)) == -1) {
		printf("Couldn't open the joystick\n");
		return -1;
	}

	ioctl(joy_fd, JSIOCGAXES , &num_of_axis);
	ioctl(joy_fd, JSIOCGBUTTONS , &num_of_buttons);
	ioctl(joy_fd, JSIOCGNAME(80), &name_of_joystick);

	printf("Joystick detected: %s\n\t%2d axis\n\t%2d buttons\n\n", name_of_joystick, num_of_axis, num_of_buttons);
	fcntl(joy_fd, F_SETFL, O_NONBLOCK); // use non-blocking methods

	printf("Opening Radio\n");
	memset(&radio, 0, sizeof(radio));
	err = sdopen(&radio, (char*)(radioPort.c_str()));
	if (err) exit(EXIT_FAILURE);
	err = sdconf(&radio);
	if (err) exit(EXIT_FAILURE);

	string rate = "Unknown";
	if (radio.settings.c_cflag & B9600 == B9600) rate = "9600 bps";
	if (radio.settings.c_cflag & B57600 == B57600) rate = "57600 bps";
	if (radio.settings.c_cflag & B115200 == B115200) rate = "115200 bps";
	printf("Radio Baud Rate: %s\n", rate.c_str());

	// Bind the socket.
	socket.bind("tcp://*:5555");
	mlen = sprintf(mbuf, "radio: ready");
	publishMessage(mbuf, mlen);

	// Do the calibration.
	doCalibration(joy_fd);

	// Start the joystick processing thread.
	thread t1(processJoystickEvents, joy_fd);
	thread t2(readRadioData);

	signal(SIGABRT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	// infinite loop.
	while (true);

	return 0;
}
