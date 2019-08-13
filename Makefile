

all:main
main:main.cpp
	g++  -std=c++0x $^ -o $@ -lboost_filesystem -lboost_system -lpthread -lboost_thread
