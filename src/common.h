/*
 * common.h
 *
 *  Created on: 2013-10-30
 *      Author: brucewoo
 */

#ifndef COMMON_H_
#define COMMON_H_

/* http request method */
enum HTTP_METHOD {
	GET = 0,
	POST,
	HEAD,
	PUT,
	DELETE,
	TRACE,
	OPTIONS,
	CONNECT,
	PATCH
};

enum CHECK_STATE {
	CHECK_STATE_REQUESTLINE = 0,
	CHECK_STATE_HEADER,
	CHECK_STATE_CONTENT
};

/* http request result */
enum HTTP_CODE {
	NO_REQUEST = 0,
	GET_REQUEST,
	BAD_REQUEST,
	NO_RESOURCE,
	FORBIDDEN_REQUEST,
	FILE_REQUEST,
	INTERNAL_ERROR,
	CLOSED_CONNECTION
};

/* line read status */
enum LINE_STATUS {
	LINE_OK = 0,
	LINE_BAD,
	LINE_OPEN
};

enum BOOL {
	FALSE = 0,
	false = 0,
	TRUE = 1,
	true = 1
};

#endif /* COMMON_H_ */
