/*
 * stress_test.c
 *
 *  Created on: 2013-11-6
 *      Author: brucewoo
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

typedef enum BOOL {
	FALSE = 0,
	false = 0,
	TRUE = 1,
	true = 1
}bool;

/* 每个客户连接不停的向服务器发送这个请求 */
static const char* request = "GET http://localhost/index.html HTTP/1.1\r\nConnection: keep-alive\r\n\r\nxxxxxxxxxxxx";

int set_nonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

void add_fd(int epollfd, int fd, bool one_shot)
{
	struct epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

	if (one_shot) {
		event.events |= EPOLLONESHOT;
	}

	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	set_nonblocking(fd);
}

/* 向服务器写入len字节的数据 */
bool write_nbytes(int sockfd, const char* buffer, int len)
{
	int bytes_write = 0;
	printf("write out %d bytes to socket %d\n", len, sockfd);
	while (true)
	{
		bytes_write = send(sockfd, buffer, len, 0);
		if (bytes_write == -1)
		{
			return false;
		}
		else if (bytes_write == 0)
		{
			return false;
		}

		len -= bytes_write;
		buffer = buffer + bytes_write;
		if (len <= 0)
		{
			return true;
		}
	}
	return true;
}

/* 从服务器读取数据 */
bool read_once(int sockfd, char* buffer, int len)
{
	int bytes_read = 0;
	memset(buffer, '\0', len);
	bytes_read = recv(sockfd, buffer, len, 0);
	if (bytes_read == -1)
	{
		return false;
	}
	else if (bytes_read == 0)
	{
		return false;
	}
	printf("read in %d bytes from socket %d with content: %s\n", bytes_read, sockfd, buffer);
	return true;
}

/* 向服务器发起num个TCP连接， 可以通过改变num来调整测试压力 */
void start_conn(int epoll_fd, int num, const char* ip, int port)
{
	int ret = 0;
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);

	int i = 0;
	for (; i<num; ++i)
	{
		sleep(1);
		int sockfd = socket(PF_INET, SOCK_STREAM, 0);
		printf("create 1 socket\n");
		if (sockfd < 0)
		{
			continue;
		}

		if (connect(sockfd, (struct sockaddr*)&address, sizeof(address)) == 0)
		{
			printf("build connection %d\n", i);
			add_fd(epoll_fd, sockfd, true);
		}
	}
}

void close_conn(int epoll_fd, int sockfd)
{
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
	close(sockfd);
}

int main(int argc, char* argv[])
{
	if (argc < 4)
	{
		printf("Usage: %s ip port conn_number\n", argv[0]);
		return 1;
	}

	int epoll_fd = epoll_create(100);
	start_conn(epoll_fd, atoi(argv[3]), argv[1], atoi(argv[2]));
	struct epoll_event events[10000];
	char buffer[2048];

	while (true)
	{
		int fds = epoll_wait(epoll_fd, events, 10000, 2000);
		int i = 0;
		for (; i<fds; i++)
		{
			int sockfd = events[i].data.fd;
			if (events[i].events & EPOLLIN)
			{
				if (!read_once(sockfd, buffer, 2048))
				{
					close_conn(epoll_fd, sockfd);
				}
				struct epoll_event event;
				event.events = EPOLLOUT | EPOLLET | EPOLLERR;
				event.data.fd = sockfd;
				epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
			}
			else if (events[i].events & EPOLLERR)
			{
				close_conn(epoll_fd, sockfd);
			}
		}
	}
}
