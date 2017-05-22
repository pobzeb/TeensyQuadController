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
#include <sys/ioctl.h>
#include <linux/joystick.h>

using namespace std;

string controllerPort;
int joy_fd, num_of_axis = 0, num_of_buttons = 0, num_of_inputs = 0;
int *inpt = NULL;
char name_of_joystick[80];
struct js_event jsBuffer[0xff];

void sighandler(int sig) {
	// App killed.
	printf("Shutting Down...\n");
	close(joy_fd);
	exit(0);
}

int map(int value, int sMin, int sMax, int dMin, int dMax) {
	return dMin + (dMax - dMin) * (value - sMin) / (sMax - sMin);
}

void processJoystickEvents(int joystick_fd) {
	// infinite loop.
	while (true) {
		// Read the joystick
		int events = read(joystick_fd, jsBuffer , sizeof(jsBuffer));
		if (events != -1) {
			events = events / sizeof(js_event);
			for (int idx = 0; idx < events; idx++) {
				inpt[jsBuffer[idx].number] = jsBuffer[idx].value;
				switch (jsBuffer[idx].type & ~ JS_EVENT_INIT) {
					case JS_EVENT_AXIS: {
						inpt[jsBuffer[idx].number] = map(inpt[jsBuffer[idx].number], -32000, 32000, -100, 100);
					}
				// 	case JS_EVENT_BUTTON: {
				// 		inpt[jsBuffer[idx].number] = jsBuffer[idx].value;
				// 	}
				}
			}

			for (int idx = 0; idx < num_of_inputs; idx++) {
				printf("%d: %d\t\t", idx, inpt[idx]);
			}
			printf("\n");
		}
	}
}

int main(int argc, char **argv) {
	// Check for valid required controller argument
	if (argc < 3 || (string)argv[1] != "-c") {
		cerr << "Usage: " << argv[0] << " -c CONTROLLER_PORT\n"
			 << "Options:\n"
			 << "\t-c\tController port (Ex: /dev/input/js0)\n"
			 << endl;
		return 1;
	}

	// Set the controller path
	controllerPort = (string)argv[2];

	// input of joystick values to variable joy_fd
	if ((joy_fd = open((char*)(controllerPort.c_str()), O_RDONLY)) == -1) {
		printf("Couldn't open the joystick\n");
		return -1;
	}

	ioctl(joy_fd, JSIOCGAXES , &num_of_axis);
	ioctl(joy_fd, JSIOCGBUTTONS , &num_of_buttons);
	ioctl(joy_fd, JSIOCGNAME(80), &name_of_joystick);

	printf("Joystick detected: %s\n\t%2d axis\n\t%2d buttons\n\n", name_of_joystick, num_of_axis, num_of_buttons);
	fcntl(joy_fd, F_SETFL, O_NONBLOCK); // use non - blocking methods

	num_of_inputs = num_of_axis + num_of_buttons;
	inpt = (int *)calloc(num_of_inputs, sizeof(int));

	// Start the joystick processing thread.
	thread t1(processJoystickEvents, joy_fd);

	signal(SIGABRT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	// infinite loop.
	while (true);

	return 0;
}
