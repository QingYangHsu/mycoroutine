#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<ctype.h>
#include<iostream>
 
 
#define SERVER_PORT 9190

#define	MAXLINE 100
int main(void) {
 
	/*定义  server端套接字文件描述符：sfd
				client套接字文件描述符：cfd
				read函数读取到的字符数：n */
	int sfd, cfd, n;
 
	/* server端地址定义（包含IP、PORT、协议族）暂未指定：server_addr
	   client端地址定义（包含IP、PORT、协议族）不需要再server.c定义，accept函数会自动填充*/
	struct sockaddr_in server_addr, client_addr;
 
	socklen_t  client_len;//为 accept函数第三个参数做准备
	char buf[MAXLINE];//接收client端发来的字符的缓冲区
 
	/*bzero：将server端置空，为了给后续的IP、PORT、协议族赋值所用
	 后续操作是为了 bind函数绑定IP、PORT和协议族的固定操作。*/
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;//IPV4
	server_addr.sin_port = htons(SERVER_PORT);//转换为网络字节序
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
 
	sfd = socket(AF_INET, SOCK_STREAM, 0);	//调用 socket函数值后会返回一个文件描述符
	bind(sfd, (struct sockaddr*)&server_addr, sizeof(server_addr));	//绑定IP、PORT、协议族

	listen(sfd, 21);
 
	/* accept函数放在 while 里面和外面的结果是不一样的，
			accept放在while里面代表客户端只能和服务器端通信一次
			accept放在while外面那么客户端就可以一直和服务器进行通信
	*/
	while (1) {
		client_len = sizeof(client_addr);
		cfd = accept(sfd, (struct sockaddr*)&client_addr, &client_len);//accept调用和会给server端返回一个和client端连接好的socket。
		std::cout<<"收到一个客户连接"<<std::endl;
		n = read(cfd, buf, MAXLINE);
 
		for (int i = 0; i < n; i++) {
			buf[i] = toupper(buf[i]);
		}
 
		write(cfd, buf, n);
		close(cfd);
	}
 
	return 0;
 
}
