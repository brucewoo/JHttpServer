/*
 * http_connect.c
 *
 *  Created on: 2013-10-30
 *      Author: brucewoo
 */
#ifndef HTTP_CONNECT_H_
#define HTTP_CONNECT_H_

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>

#include "common.h"
#include "queue.h"

/* filename max length */
#define FILENAME_LEN 200
/* read buffer size */
#define READ_BUFFER_SIZE 2048
/* write buffer size */
#define WRITE_BUFFER_SIZE 1024

typedef enum BOOL bool;
typedef enum HTTP_CODE http_code;
typedef enum LINE_STATUS line_status;
typedef enum CHECK_STATE check_state;
typedef enum HTTP_METHOD http_method;

extern int epollfd;
extern int user_count;

struct http_conn {
	queue_t head;

	int sockfd;						//该HTTP连接的socket
	struct sockaddr_in address;		//对方的socket地址

	char read_buf[READ_BUFFER_SIZE];//读缓冲区
	int read_index;					//标识读缓冲中已经读入的客户端数据的最后一个字节的下一个位置
	int check_index;				//当前正在分析的字符在缓冲区中的位置
	int start_line;					//当前正在解析的行的起始位置
	char write_buf[WRITE_BUFFER_SIZE];				//想写缓冲区
	int write_index;				//写缓冲区待发送的字节数
	check_state curr_state;			//主状态机当前所处的状态
	http_method method;				//请求方法

	char real_file[FILENAME_LEN];	//客户端请求的目标文件的完整路径，其内容等于doc_root + url, doc_root是网站根目录
	char* url;						//客户请求的目标文件的文件名
	char* version;					//HTTP协议版本号，支持http/1.1
	char* host;						//主机名
	int content_length;				//HTTP请求的消息体的长度
	bool linger;					//HTTP请求是否要求保持连接

	char* file_address;				//客户请求的目标文件被mmap到内存中的起始位置
	struct stat file_stat;			//目标文件的状态，通过它可以判断文件是否存在，是否为目录，是否可读，并获取文件大小等信息
	struct iovec iv[2];				//使用writev来执行写操作，所以定义下面两个成员，其中iv_count表示被写内存块的数量
	int iv_count;
};

typedef struct http_conn http_conn;

/* initialize new accept connection */
void init_new_connect(http_conn* conn, int sockfd, const struct sockaddr_in* addr);

void init(http_conn* conn);

/* close connection */
void close_connect(http_conn* conn);

/* process client requst */
void process(http_conn* conn);

/* noblocking read */
bool http_conn_read(http_conn* conn);

/* noblocking write */
bool http_conn_write(http_conn* conn);

/* parse request*/
http_code parse_request(http_conn* conn);

/* fill respond */
bool fill_respond(http_conn* conn, http_code ret);

/* parse http request */
http_code parse_request_line(http_conn* conn, char* text);

http_code parse_headers(http_conn* conn, char* text);

http_code parse_content(http_conn* conn, char* text);

http_code do_request(http_conn* conn);

char* get_line(http_conn* conn);

line_status parse_line(http_conn* conn);

void unmap(http_conn* conn);

bool add_reponse(http_conn* conn, const char* format, ...);

bool add_content(http_conn* conn, const char* content);

bool add_status_line(http_conn* conn, int status, const char* title);

bool add_headers(http_conn* conn, int content_length);

bool add_content_length(http_conn* conn, int content_length);

bool add_linger(http_conn* conn);

bool add_blank_line(http_conn* conn);

#endif

