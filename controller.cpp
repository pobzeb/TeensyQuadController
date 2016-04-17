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
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include "serial.h"

// Define the length of a message.
#define MESSAGE_LENGTH 8

// Define the length of a keyboard message.
#define KEYBOARD_MESSAGE_LENGTH 6

// Define the length of a heartbeat message and interval in seconds.
#define HEARTBEAT_MESSAGE_LENGTH 2
#define HEARTBEAT_INTERVAL 2

// Min and max values of controller.
#define MIN_VALUE -32767
#define MAX_VALUE 32767

using namespace std;

string controllerPort, radioPort;

sd_t radio;
int err;
int value;
bool newEvent = false;

int joy_fd, num_of_axis = 0, num_of_buttons = 0, pid_mode = 0, pid_type = 0;
float* offsets = NULL;
string radioIn;

char *button = NULL, name_of_joystick[80];
struct js_event jsBuffer[0xff];

// Create a ZMQ socket to publish.
//zmq::context_t context(1);
//zmq::socket_t socket(context, ZMQ_PUB);
char mbuf[1024];
int mlen = 0;
unsigned char* float2Array(float fval) {
	unsigned char* ret_array = NULL;
	ret_array = (unsigned char*)calloc(4, sizeof(unsigned char));
	union {
		float  float_var;
		unsigned char temp_array[4];
	} u;

	u.float_var = fval;
	memcpy(ret_array, u.temp_array, sizeof ret_array);

	return ret_array;
}

string int2String( const int &n ) {
	ostringstream stm;
	stm << n;
	return stm.str();
}

const char* byte2Binary(short x) {
	static char b[33];
	b[32] = '\0';

	for (int z = 0; z < 32; z++) {
		b[31 - z] = ((x >> z) & 0x1) ? '1' : '0';
	}

	return b;
}

// Mapping function.
float map(int value, int s_low, int s_high, int d_low, int d_high) {
	return d_low + (d_high - d_low) * (value - s_low) / (s_high - s_low);
}

void calibrateJoystick() {
	// Read the joystick.
	int events = read(joy_fd, jsBuffer , sizeof(jsBuffer));
	if (events != -1) {
		events = events / sizeof(js_event);
		for (int idx = 0; idx < events; idx++) {
			// See what kind of event this is.
			switch (jsBuffer[idx].type & ~ JS_EVENT_INIT) {
				case JS_EVENT_AXIS :
					offsets[jsBuffer[idx].number] = 0.0f - jsBuffer[idx].value;
					break;
				case JS_EVENT_BUTTON :
					button [jsBuffer[idx].number ] = jsBuffer[idx].value;
			}
		}
	}
//	printf("Throttle Offset: %6.3f\tSpin Offset: %6.3f\tMove Left/Right Offset: %6.3f\tMove Forward/Backward Offset: %6.3f\tLeft Trigger Offset: %6.3f\tRight Trigger Offset: %6.3f\n", offsets[0], offsets[1], offsets[2], offsets[3], offsets[4], offsets[5]);
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
	printf("%s\n", buf);
//	socket.send(message);
}

void processEvent(unsigned char* cmd, int messageLength) {
	// New value, send it over the radio.
	err = write(radio.fd, cmd, messageLength);
	if (err == -1) {
		fprintf(stderr, "Failed to write %s: %s\n", radioPort.c_str(), strerror(errno));
		exit(EXIT_FAILURE);
	}

	fflush(stdout);
}

void readRadioData() {
	int bytesAvailable = 0;
	int bufSize = 32, bufIdx = 0;
	unsigned char buf[bufSize];
	char rb = ' ';
	int resp[7];
	bool messageReady = false;

	memset(&buf[0], 0, bufSize);
	bytesAvailable = read(radio.fd, buf, bufSize);
	int state = 0;
	int b = 0;
	while (true) {
		if (bytesAvailable > 0) {
			while (bytesAvailable > 0) {
				if (state == 0) {
					b = 0;
					messageReady = false;
					rb = (char)buf[bufIdx++];
					bytesAvailable--;

					// Check for the start of a message.
					if (rb == 'm') state = 1;
					else state = 0;
					continue;
				}
				else if (state == 1) {
					// Read a byte.
					resp[b] = buf[bufIdx++];
					bytesAvailable--;
			printf("B: %d: %c\n", b, resp[b]);

					if ((char)resp[b] == 'm') {
						messageReady = false;
						b = 0;
						continue;
					}

					b++;

					if (b == 6) messageReady = true;
				}

				if (messageReady == true) {
					for (b = 0; b < 6; b++) {
						printf("%c", resp[b]);
					}
					printf("\n");
					state = 0;
				}

				memset(&buf[0], 0, bufSize);
				bytesAvailable = read(radio.fd, buf, bufSize);
				bufIdx = 0;
			}
		}

		memset(&buf[0], 0, bufSize);
		bytesAvailable = read(radio.fd, buf, bufSize);
		bufIdx = 0;
	}
}

void processJoystickEvents(int joystick_fd) {
	// Array to hold current controller state (one byte per state).
	// [Msg Char, throttle, yaw, pitch, roll, l_trigger, r_trigger, buttons(Left, Right, Up, Down, X, Y, A, B)]
	unsigned char* cmd = NULL;
	cmd = (unsigned char*)calloc(MESSAGE_LENGTH, sizeof(unsigned char));

	// infinite loop.
	int lBtn = 0, rBtn = 0, uBtn = 0, dBtn = 0;
	int xBtn = 0, yBtn = 0, aBtn = 0, bBtn = 0;
	while (true) {
		// Reset the new event flag.
		newEvent = false;

		// Read the joystick
		int events = read(joystick_fd, jsBuffer , sizeof(jsBuffer));
		if (events != -1) {
			events = events / sizeof(js_event);
			for (int idx = 0; idx < events; idx++) {
				// See what to do with the event
				unsigned char sum = 0;
				switch (jsBuffer[idx].type & ~ JS_EVENT_INIT) {
					case JS_EVENT_AXIS: {
						// Adjust for offsets and map value to single byte.
						value = jsBuffer[idx].value + offsets[jsBuffer[idx].number];
						value = map(value, MIN_VALUE, MAX_VALUE, 0, 255);

						// [throttle, yaw, pitch, roll, l_trigger, r_trigger, directional_buttons(Left, Right, Up, Down), buttons(Start, Select, L_Button, R_Button, A, B, X, Y)]
						if (jsBuffer[idx].number == 1) {
							// Throttle
							value = (jsBuffer[idx].value + offsets[jsBuffer[idx].number]) * -1.0f;
							value = max(0, value);
							value = map(value, 0.0f, MAX_VALUE, 0, 255);
							if (cmd[1] != value) {
								cmd[1] = value;
								newEvent = true;
							}
						}
						else if (jsBuffer[idx].number == 0 && cmd[2] != value) {
							// Yaw
							cmd[2] = 255 - value;
							newEvent = true;
						}
						else if (jsBuffer[idx].number == 4 && cmd[3] != value) {
							// Pitch
							cmd[3] = value;
							newEvent = true;
						}
						else if (jsBuffer[idx].number == 3 && cmd[4] != value) {
							// Roll
							cmd[4] = 255 - value;
							newEvent = true;
						}
						else if (jsBuffer[idx].number == 2 && cmd[5] != value) {
							// Left Trigger
							cmd[5] = value;
							newEvent = true;
						}
						else if (jsBuffer[idx].number == 5 && cmd[6] != value) {
							// Right Trigger
							cmd[6] = value;
							newEvent = true;
						}
						else if (jsBuffer[idx].number == 6) {
							newEvent = true;
							if (jsBuffer[idx].value < 0) {
								// Left Directional Button
								lBtn = 1;
								rBtn = 0;
							}
							else if (jsBuffer[idx].value > 0) {
								// Right Directional Button
								lBtn = 0;
								rBtn = 1;
							}
							else {
								lBtn = 0;
								rBtn = 0;
							}
						}
						else if (jsBuffer[idx].number == 7) {
							newEvent = true;
							if (jsBuffer[idx].value < 0) {
								// Up Directional Button
								uBtn = 1;
								dBtn = 0;
							}
							else if (jsBuffer[idx].value > 0) {
								// Down Directional Button
								uBtn = 0;
								dBtn = 1;
							}
							else {
								uBtn = 0;
								dBtn = 0;
							}
						}

						break;
					}
					case JS_EVENT_BUTTON: {
						button [jsBuffer[idx].number ] = jsBuffer[idx].value;

						if (jsBuffer[idx].number == 0) {
							newEvent = true;
							aBtn = jsBuffer[idx].value;
						}
						else if (jsBuffer[idx].number == 1) {
							newEvent = true;
							bBtn = jsBuffer[idx].value;
						}
						else if (jsBuffer[idx].number == 2) {
							newEvent = true;
							xBtn = jsBuffer[idx].value;
						}
						else if (jsBuffer[idx].number == 3) {
							newEvent = true;
							yBtn = jsBuffer[idx].value;
						}
					}
				}

				// mlen = sprintf(mbuf, "Throttle: %3f\tSpin: %3f\tMove Left/Right: %3f\tMove Forward/Backward: %3f\tLeft Trigger: %3f\tRight Trigger: %3f\n", throttle, spin, move_left_right, move_forward_backward, left_trigger, right_trigger);
				// publishMessage(mbuf, mlen);
				// mlen = sprintf(mbuf, "Button: %d\tValue: %d\n", jsBuffer[idx].number, jsBuffer[idx].value);
				// publishMessage(mbuf, mlen);
				// printf("\t\t\t\tThrottle: %3f\tSpin: %3f\tMove Left/Right: %3f\tMove Forward/Backward: %3f\tLeft Trigger: %3f\tRight Trigger: %3f\n", throttle, spin, move_left_right, move_forward_backward, left_trigger, right_trigger);
				// printf("\t\t\t\tButton: %d\tValue: %d\n", jsBuffer[idx].number, jsBuffer[idx].value);

				if (newEvent == true) {
					cmd[0] = 'm';
					sum += lBtn << 7;
					sum += rBtn << 6;
					sum += uBtn << 5;
					sum += dBtn << 4;
					sum += xBtn << 3;
					sum += yBtn << 2;
					sum += aBtn << 1;
					sum += bBtn << 0;
					cmd[7] = sum;
					printf("CMD: Trottle: %3d, Yaw: %3d, Pitch: %3d, Roll: %3d, L_Trigger: %3d, R_Trigger: %3d, Buttons: %s\n", cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], byte2Binary(cmd[7]));
					processEvent(cmd, MESSAGE_LENGTH);
				}
			}
		}

		if (errno != EAGAIN) {
			/* Error */
		}
	}
}

void readKeyboardInput() {
	// Array to hold current keyboard message.
	// [Msg Char, command, valueByte1, valueByte2, valueByte3, valueByte4)]
	unsigned char* cmd = NULL;
	cmd = (unsigned char*)calloc(KEYBOARD_MESSAGE_LENGTH, sizeof(unsigned char));
	string kInput;
	float value = 0.0f;
	unsigned char* valuePtr;
	valuePtr = (unsigned char*)&value;
	while (true) {
		// Read keyboard input.
		cin >> kInput;
		printf("Keyboard Input: %s\n", kInput.c_str());

		// Reset the command array.
		memset(&cmd[0], 0, sizeof(cmd));

		// Convert the value to bytes.
		value = stof(kInput.substr(1));

		// Build the command.
		cmd[0] = 'k';
		cmd[1] = (kInput.substr(0,1).c_str())[0];
		for (int cIdx = 0; cIdx < 4; cIdx++) cmd[cIdx + 2] = valuePtr[cIdx];
		printf("Command: %c 0x%02X 0x%02X 0x%02X 0x%02X  End: %f\n", cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], value);

		// Send the command.
		processEvent(cmd, KEYBOARD_MESSAGE_LENGTH);
	}
}

void doHeartbeat() {
	// Array to hold current heartbeat message.
	// [Msg Char, byte)]
	unsigned char* cmd = NULL;
	cmd = (unsigned char*)calloc(HEARTBEAT_MESSAGE_LENGTH, sizeof(unsigned char));
	while (true) {
		// build a command.
		cmd[0] = 'h';
		cmd[1] = '0';
		printf("HEARTBEAT\n");

		// Send the command.
		processEvent(cmd, HEARTBEAT_MESSAGE_LENGTH);

		// Wait.
		sleep(HEARTBEAT_INTERVAL);
	}
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

	offsets = (float*)calloc(6, sizeof(float));
	button = (char *)calloc(num_of_buttons, sizeof(char));

	printf("Joystick detected: %s\n\t%2d axis\n\t%2d buttons\n\n", name_of_joystick, num_of_axis, num_of_buttons);
	fcntl(joy_fd, F_SETFL, O_NONBLOCK); // use non - blocking methods

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

	// Calibrate the joysticks.
	//calibrateJoystick();

	// Wait for a newline char.
	mlen = sprintf(mbuf, "radio: clearing radio");
	publishMessage(mbuf, mlen);
	int len = 0;
	unsigned char byte;
//	while (len <= 0) {
//		len = read(radio.fd, &byte, 1);
//		if (len > 0 && (char)byte == '\n') break;
//		else len = 0;
//	}

	// Bind the socket.
//	socket.bind("tcp://*:5555");

	mlen = sprintf(mbuf, "radio: ready");
	publishMessage(mbuf, mlen);

	// Start the joystick processing thread.
	thread t1(processJoystickEvents, joy_fd);
	thread t2(readRadioData);
	thread t3(readKeyboardInput);
	thread t4(doHeartbeat);

	signal(SIGABRT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	// infinite loop.
	while (true);

	return 0;
}
