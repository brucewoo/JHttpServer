/*
 * http_connect.c
 *
 *  Created on: 2013-10-30
 *      Author: brucewoo
 */

#include "http_connect.h"

/* http respond status information */
const char* ok_200_title = "OK";
const char* error_400_title = "bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satify.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/var/www/html";

int epollfd = -1;	//所有socket上的事件都被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为全局的
int user_count = 0;	//统计用户数量

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

void remove_fd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

void mod_fd(int epollfd, int fd, int ev)
{
	struct epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* initialize new accept connection */
void init_new_connect(http_conn* conn, int sockfd, const struct sockaddr_in* addr)
{
	conn->sockfd = sockfd;
	conn->address = *addr;

	int reuse = 1;
	setsockopt(conn->sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	add_fd(epollfd, conn->sockfd, TRUE);
	user_count++;

	printf("current user count : [%d]\n", user_count);
	init(conn);
}

void init(http_conn* conn)
{
	conn->curr_state = CHECK_STATE_REQUESTLINE;
	conn->linger = FALSE;

	conn->method = GET;
	conn->url = NULL;
	conn->version = NULL;
	conn->content_length = 0;
	conn->host = NULL;
	conn->check_index = 0;
	conn->read_index = 0;
	conn->write_index = 0;

	memset(conn->read_buf, '\0', READ_BUFFER_SIZE);
	memset(conn->write_buf, '\0', WRITE_BUFFER_SIZE);
	memset(conn->real_file, '\0', FILENAME_LEN);
}

void close_connect(http_conn* conn)
{
	if (conn->sockfd != -1)
	{
		remove_fd(epollfd, conn->sockfd);
		conn->sockfd = -1;
		user_count--;
		printf("current user count : [%d]\n", user_count);
	}
}

/* 由线程池中的工作线程调用， 这是处理HTTP请求的入口函数 */
void process(http_conn* conn)
{
	http_code read_ret = parse_request(conn);
	if (read_ret == NO_REQUEST)
	{
		mod_fd(epollfd, conn->sockfd, EPOLLIN);
		return ;
	}

	bool write_ret = fill_respond(conn, read_ret);
	if (!write_ret)
	{
		close_connect(conn);
	}

	mod_fd(epollfd, conn->sockfd, EPOLLOUT);
}

bool http_conn_read(http_conn* conn)
{
	if (conn->read_index >= READ_BUFFER_SIZE)
	{
		return FALSE;
	}

	int bytes_read = 0;
	while (TRUE)
	{
		bytes_read = recv(conn->sockfd, conn->read_buf+conn->read_index,
				READ_BUFFER_SIZE-conn->read_index, 0);
		if (bytes_read == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				break;
			}
			return FALSE;
		}
		else if (bytes_read == 0)
		{
			return FALSE;
		}

		conn->read_index += bytes_read;
	}
	return TRUE;
}

bool http_conn_write(http_conn* conn)
{
	int temp = 0;
	int bytes_have_send = 0;
	int bytes_to_send =conn->write_index;
	if (bytes_to_send == 0)
	{
		mod_fd(epollfd, conn->sockfd, EPOLLIN);
		init(conn);
		return TRUE;
	}

	while (TRUE)
	{
		temp = writev(conn->sockfd, conn->iv, conn->iv_count);
		if (temp == -1)
		{
			/* 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件。虽然在此期间，
			 * 服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性 */
			if (errno == EAGAIN)
			{
				mod_fd(epollfd, conn->sockfd, EPOLLOUT);
				return TRUE;
			}
			unmap(conn);
			return FALSE;
		}

		bytes_to_send -= temp;
		bytes_have_send += temp;
		if (bytes_to_send <= bytes_have_send)
		{
			/* 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接 */
			unmap(conn);
			if (conn->linger)
			{
				init(conn);
				mod_fd(epollfd, conn->sockfd, EPOLLIN);
				return TRUE;
			}
			else
			{
				mod_fd(epollfd, conn->sockfd, EPOLLIN);
				return FALSE;
			}
		}
	}
	return TRUE;
}

http_code parse_request(http_conn* conn)
{
	line_status status = LINE_OK;
	http_code ret = NO_REQUEST;
	char* text = 0;

	while (((conn->curr_state == CHECK_STATE_CONTENT) && (status == LINE_OK))
			|| ((status=parse_line(conn)) == LINE_OK))
	{
		text = get_line(conn);
		conn->start_line = conn->check_index;
		printf("got one http line : %s\n", text);

		switch (conn->curr_state)
		{
		case CHECK_STATE_REQUESTLINE:
			ret = parse_request_line(conn, text);
			if (ret == BAD_REQUEST)
			{
				return ret;
			}
			break;
		case CHECK_STATE_HEADER:
			ret = parse_headers(conn, text);
			if (ret == BAD_REQUEST)
			{
				return ret;
			}
			else if (ret == GET_REQUEST)
			{
				return do_request(conn);
			}
			break;
		case CHECK_STATE_CONTENT:
			ret = parse_content(conn, text);
			if (ret == GET_REQUEST)
			{
				return do_request(conn);;
			}
			status = LINE_OPEN;
			break;
		default:
			return INTERNAL_ERROR;
		}
	}
	return NO_REQUEST;
}

/* 根据服务器处理HTTP请求的结果， 决定返回给客户端的内容 */
bool fill_respond(http_conn* conn, http_code ret)
{
	switch (ret)
	{
	case INTERNAL_ERROR:
		add_status_line(conn, 500, error_500_title);
		add_headers(conn, strlen(error_500_form));
		if (!add_content(conn, error_500_form)) {
			return FALSE;
		}
		break;
	case BAD_REQUEST:
		add_status_line(conn, 400, error_400_title);
		add_headers(conn, strlen(error_400_form));
		if (!add_content(conn, error_400_form)) {
			return FALSE;
		}
		break;
	case NO_RESOURCE:
		add_status_line(conn, 404, error_404_title);
		add_headers(conn, strlen(error_404_form));
		if (!add_content(conn, error_404_form)) {
			return FALSE;
		}
		break;
	case FORBIDDEN_REQUEST:
		add_status_line(conn, 403, error_403_title);
		add_headers(conn, strlen(error_403_form));
		if (!add_content(conn, error_403_form)) {
			return FALSE;
		}
		break;
	case FILE_REQUEST:
		add_status_line(conn, 200, ok_200_title);
		if (conn->file_stat.st_size != 0)
		{
			add_headers(conn, conn->file_stat.st_size);
			conn->iv[0].iov_base = conn->write_buf;
			conn->iv[0].iov_len = conn->write_index;
			conn->iv[1].iov_base = conn->file_address;
			conn->iv[1].iov_len = conn->file_stat.st_size;
			conn->iv_count = 2;
			return TRUE;
		}
		else
		{
			const char* ok_string = "<html><body></body></html>";
			add_headers(conn, strlen(ok_string));
			if (!add_content(conn, ok_string))
			{
				return FALSE;
			}
		}
		break;
	default :
		return FALSE;
	}

	conn->iv[0].iov_base = conn->write_buf;
	conn->iv[0].iov_len = conn->write_index;
	conn->iv_count = 1;
	return TRUE;
}

/* 解析HTTP请求行，获得请求方法，目标URL，以及HTTP版本号 */
http_code parse_request_line(http_conn* conn, char* text)
{
	conn->url = strpbrk(text, " \t");
	if (!conn->url)
	{
		return BAD_REQUEST;
	}
	*conn->url++ ='\0';

	char* method = text;
	if (strcasecmp(method, "GET") == 0)
	{
		conn->method = GET;
	}
	else if (strcasecmp(method, "POST") == 0)
	{
		conn->method = POST;
	}
	else if (strcasecmp(method, "HEAD") == 0)
	{
		conn->method = HEAD;
	}
	else if (strcasecmp(method, "PUT") == 0)
	{
		conn->method = PUT;
	}
	else if (strcasecmp(method, "DELETE") == 0)
	{
		conn->method = DELETE;
	}
	else if (strcasecmp(method, "TRACE") == 0)
	{
		conn->method = TRACE;
	}
	else if (strcasecmp(method, "OPTIONS") == 0)
	{
		conn->method = OPTIONS;
	}
	else if (strcasecmp(method, "CONNECT") == 0)
	{
		conn->method = CONNECT;
	}
	else if (strcasecmp(method, "PATCH") == 0)
	{
		conn->method = PATCH;
	}
	else
	{
		return BAD_REQUEST;
	}

	conn->url += strspn(conn->url, " \t");
	conn->version = strpbrk(conn->url, " \t");
	if (!conn->version)
	{
		return BAD_REQUEST;
	}
	*conn->version++ = '\0';
	conn->version += strspn(conn->version, " \t");
	if (strcasecmp(conn->version, "HTTP/1.1") != 0)
	{
		return BAD_REQUEST;
	}
	if (strncasecmp(conn->url, "http://", 7) == 0)
	{
		conn->url += 7;
		conn->url = strchr(conn->url, '/');
	}
	if (!conn->url || conn->url[0] !=  '/')
	{
		return BAD_REQUEST;
	}

	conn->curr_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

/* 解析HTTP请求的一个头部信息 */
http_code parse_headers(http_conn* conn, char* text)
{
	/* 遇到空行表示头部字段解析完成 */
	if (text[0] == '\0')
	{
		/* 如果HTTP请求有消息体， 则还需要读取conn->content_length字节的消息体，
		 *  状态机转移到CHECK_STATE_CON TENT */
		if (conn->content_length != 0)
		{
			conn->curr_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		/* 否则说明已经得到了一个完整的HTTP请求 */
		return GET_REQUEST;
	}
	/* 处理Connection头部字段 */
	else if (strncasecmp(text, "Connection:", 11) == 0)
	{
		text += 11;
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0)
		{
			conn->linger = TRUE;
		}
	}
	/* 处理Content-Length头部字段 */
	else if (strncasecmp(text, "Content-Length:", 15) == 0)
	{
		text += 15;
		text += strspn(text, " \t");
		conn->content_length = atol(text);
	}
	/* 处理Host头部字段 */
	else if (strncasecmp(text, "Host:", 5) == 0)
	{
		text += 5;
		text += strspn(text, " \t");
		conn->host = text;
	}
	else
	{
		printf("oop! unknow header %s\n", text);
	}

	return NO_REQUEST;
}

/* 没有解析真正HTTP请求的消息体， 只是判断它是否被完整的读入了 */
http_code parse_content(http_conn* conn, char * text)
{
	if (conn->read_index >= (conn->content_length + conn->check_index))
	{
		text[conn->content_length] = '\0';
		return GET_REQUEST;
	}

	return NO_REQUEST;
}

/* 当得到一个完整，正确的HTTP请求时， 就分析目标文件的属性，如果目标文件存在，对所有用户可读，
 * 并且不是目录，则使用mmap将其映射到内存地址file_address处，并告诉调用者获取文件成功 */
http_code do_request(http_conn* conn)
{
	strcpy(conn->real_file, doc_root);
	int len = strlen(doc_root);
	strncpy(conn->real_file + len, conn->url, FILENAME_LEN - len - 1);
	if (stat(conn->real_file, &conn->file_stat) < 0)
	{
		return NO_RESOURCE;
	}

	if (!(conn->file_stat.st_mode & S_IROTH))
	{
		return BAD_REQUEST;
	}

	if (S_ISDIR(conn->file_stat.st_mode))
	{
		return BAD_REQUEST;
	}

	int fd = open(conn->real_file, O_RDONLY);
	conn->file_address = (char *)mmap(0, conn->file_stat.st_size, PROT_READ,
			MAP_PRIVATE, fd, 0);
	close(fd);
	return FILE_REQUEST;
}

char* get_line(http_conn* conn)
{
	return conn->read_buf + conn->start_line;
}

line_status parse_line(http_conn* conn)
{
	char temp;
	for (; conn->check_index<conn->read_index; ++conn->check_index)
	{
		temp = conn->read_buf[conn->check_index];
		if (temp == '\r')
		{
			if ((conn->check_index + 1) == conn->read_index)
			{
				return LINE_OPEN;
			}
			else if (conn->read_buf[conn->check_index] == '\n')
			{
				conn->read_buf[conn->check_index++] = '\0';
				conn->read_buf[conn->check_index++] = '\0';
				return LINE_OK;
			}

			return LINE_BAD;
		}
		else if (temp == '\n')
		{
			if ((conn->check_index > 1) && (conn->read_buf[conn->check_index - 1] == '\r'))
			{
				conn->read_buf[conn->check_index-1] = '\0';
				conn->read_buf[conn->check_index++] = '\0';
				return LINE_OK;
			}

			return LINE_BAD;
		}
	}
	return LINE_OPEN;
}

void unmap(http_conn* conn)
{
	if (conn->file_address)
	{
		munmap(conn->file_address, conn->file_stat.st_size);
		conn->file_address = NULL;
	}
}

bool add_reponse(http_conn* conn, const char* format, ...)
{
	if (conn->write_index >= WRITE_BUFFER_SIZE)
	{
		return FALSE;
	}

	va_list arg_list;
	va_start(arg_list, format);
	int len = vsnprintf(conn->write_buf+conn->write_index,
			WRITE_BUFFER_SIZE-1-conn->write_index, format, arg_list);
	if (len >= (WRITE_BUFFER_SIZE-1-conn->write_index))
	{
		return FALSE;
	}
	conn->write_index += len;
	va_end(arg_list);
	return TRUE;
}

bool add_content(http_conn* conn, const char* content)
{
	return add_reponse(conn, "%s", content);
}

bool add_status_line(http_conn* conn, int status, const char* title)
{
	return add_reponse(conn, "%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool add_headers(http_conn* conn, int content_length)
{
	if (add_content_length(conn, conn->content_length)
			&& add_linger(conn) && add_blank_line(conn))
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

bool add_content_length(http_conn* conn, int content_length)
{
	return add_reponse(conn, "Content-Length: %s\r\n", content_length);
}

bool add_linger(http_conn* conn)
{
	return add_reponse(conn, "Connection: %s\r\n",
			conn->linger ? "keep-alive" : "close");
}

bool add_blank_line(http_conn* conn)
{
	return add_reponse(conn, "%s", "\r\n");
}
