#include <zmq.hpp>
#include <string>
#include <iostream>

char message[10];

int main() {
	zmq::context_t context(1);
	zmq::socket_t socket(context, ZMQ_SUB);

	std::cout << "Connecting to hello world Publisher..." << std::endl;
	socket.connect("tcp://localhost:5555");
	socket.setsockopt(ZMQ_SUBSCRIBE, "", 0);

	for (int idx = 0; idx < 10; idx++) {
		zmq::message_t reply;
		socket.recv(&reply);
		memcpy(&message, reply.data(), reply.size());
		std::cout << "Received " << message << std::endl;
	}

	return 0;
}
