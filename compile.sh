#!/bin/bash

g++ -g -o teensyQuadController controller.cpp serial.cpp -lm -lrt -std=c++0x -lpthread -lzmq
g++ -g -o zmqserver zmqserver.cpp -lm -lrt -lpthread -lzmq
g++ -g -o zmqclient zmqclient.cpp -lm -lrt -lpthread -lzmq
