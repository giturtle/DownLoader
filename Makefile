

all:client server
client:client.cpp
	g++  -std=c++0x $^ -o $@ -lboost_filesystem -lboost_system -lpthread -lboost_thread
server:server.cpp
	g++  -std=c++0x -g $^ -o $@ -lboost_filesystem -lboost_system -lpthread
