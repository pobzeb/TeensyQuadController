#include <zmq.hpp>
#include <string>
#include <cstdio>
#include <iostream>
#include <unistd.h>

char buf[10];
int counter = 0, len = 0;

int main() {
	zmq::context_t context(1);
	zmq::socket_t socket(context, ZMQ_PUB);
	socket.bind("tcp://*:5555");

	while (true) {
		len = sprintf(buf, "World %d", counter);
		zmq::message_t message(len);
		memcpy(message.data(), buf, len);
		std::cout << "Sending " << buf << std::endl;
		socket.send(message);

		sleep(2);
		counter++;
	}

	return 0;
}
