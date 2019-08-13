
//主线程运行客户端，将服务端设置为后台进程

#include "client.hpp"
#include "server.hpp"

void srv_start(){
	P2PServer server;
	server.Start(9000);
}

int main(){
	std::thread thr(srv_start);
	thr.detach();		//不关心返回值
	P2PClient client(9000);		//前台进程
	client.Start();
	return 0;
}
