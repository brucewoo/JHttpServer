/*
 * http_connect.h
 *
 *  Created on: 2013-11-3
 *      Author: brucewoo
 */

#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include "http_connect.h"
#include "thread_pool.h"
#include "log.h"

void add_signal(int signal, void (handler)(int), bool restart);
void show_error(int conn_fd, const char* info);

extern log_handle_t g_log;

#endif
