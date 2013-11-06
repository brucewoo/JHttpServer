#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "jhttpserver.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int epollfd;
extern int user_count;
log_handle_t g_log;

extern int add_fd(int epollfd, int fd, bool one_shot);

void add_signal(int signal, void (handler)(int), bool restart)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if (restart)
	{
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(signal, &sa, NULL) != -1);
}

void show_error(int conn_fd, const char* info)
{
	ERROR(&g_log, "jhttpserver", "%s", info);
	send(conn_fd, info, strlen(info), 0);
	close(conn_fd);
}

int main(int argc, char* argv[])
{
	if (argc <= 2)
	{
		printf("Usage: %s ip_address port_number\n", argv[0]);
		return 0;
	}

	log_globals_init(&g_log);
	log_init(&g_log, "jhttpserver.log", NULL);
	log_set_loglevel(&g_log, LOG_DEBUG);

	const char* ip = argv[1];
	int port = atoi(argv[2]);

	INFO(&g_log, "jhttpserver", "%s : %d", ip, port);

	add_signal(SIGPIPE, SIG_IGN, TRUE);

	thread_pool* pool = create_thread_pool(8 ,10000);
	if (pool == NULL)
	{
		printf("create thread pool is failed.");
		ERROR(&g_log, "jhttpserver", "create thread pool is failed.");
		return 1;
	}

	http_conn* users = (http_conn*)malloc(sizeof(http_conn) * MAX_FD);
	assert(users);

	int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	struct linger tmp = {1, 0};
	setsockopt(listen_fd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

	int ret = 0;
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	inet_pton(AF_INET, ip, &address.sin_addr);

	ret = bind(listen_fd, (struct sockaddr *)&address, sizeof(address));
	assert(ret >= 0);
	ret = listen(listen_fd, 5);
	assert(ret >= 0);

	struct epoll_event events[MAX_EVENT_NUMBER];
	epollfd = epoll_create(5);
	assert(epollfd != -1);
	add_fd(epollfd, listen_fd, false);

	while (true)
	{
		int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if ((number < 0) && (errno != EINTR))
		{
			printf("epoll failure\n");
			break;
		}

		int i=0;
		for (; i<number; i++)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == listen_fd)
			{
				struct sockaddr_in client_address;
				socklen_t client_addr_len = sizeof(client_address);
				int conn_fd = accept(listen_fd, (struct sockaddr*)&client_address,
						&client_addr_len);
				if (conn_fd < 0)
				{
					printf("errno is : %d\n", errno);
					continue;
				}
				if (user_count > MAX_FD)
				{
					show_error(conn_fd, "Internal server busy");
					continue;
				}
				init_new_connect(&users[conn_fd], conn_fd, &client_address);

			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
			{
				close_connect(&users[sockfd]);
			}
			else if (events[i].events & EPOLLIN)
			{
				if (http_conn_read(&users[sockfd]))
				{
					add_conn(pool, users + sockfd);
				}
				else
				{
					close_connect(&users[sockfd]);
				}
			}
			else if (events[i].events & EPOLLOUT)
			{
				if (!http_conn_write(&users[sockfd]))
				{
					close_connect(&users[sockfd]);
				}
			}
		}
	}

	close(epollfd);
	close(listen_fd);
	free(users);
	destroy_thread_pool(pool);
	return 0;
}

