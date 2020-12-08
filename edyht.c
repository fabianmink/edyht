/*
  Copyright (c) 2014-2020 Fabian Mink <fabian.mink@mink-ing.de>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file edyht.c
 * @author Fabian Mink
 * @date 2017-07-25
 * @brief edyht - Embedded DYnamic Http server
 * @copyright BSD 2-Clause License
 *
 * Embedded DYnamic Http server for use with the lwIP TCP/IP stack.
 * Partially based on the httpserver-netconn example of lwIP contribution package
 *
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "lwip/stats.h"
#include "edyht.h"
#include "FreeRTOS.h"
#include "task.h"


//Generate file.*i by "xxd -i infile.* outfile.*i"
#include "htdocs/index.htmi"
#include "htdocs/err404.htmi"
#include "htdocs/credits.htmi"
#include "htdocs/testform_begin.htmi"
#include "htdocs/testform_end.htmi"
#include "htdocs/tasks_begin.htmi"
#include "htdocs/tasks_end.htmi"
#include "htdocs/lwip_begin.htmi"
#include "htdocs/lwip_end.htmi"

static void page_FreeRTOS_Tasks(struct netconn *conn);
//static void page_LwIP_Info(struct netconn *conn);

#define EDYHT_PRIO    ( tskIDLE_PRIORITY + 3 )

/* HTTP/1.0 200 OK */
static const unsigned char http_200ok[] = {
		0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x30, 0x20, 0x32, 0x30, 0x30,
		0x20, 0x4f, 0x4b, 0x0d, 0x0a
};
static const unsigned int http_200ok_len = 17;

/* HTTP/1.0 202 Accepted */
static const unsigned char http_202acc[] = {
		0x48,0x54,0x54,0x50,0x2f,0x31,0x2e,0x30,0x20,0x32,0x30,0x32,0x20,0x41,
		0x63,0x63,0x65,0x70,0x74,0x65,0x64,0x0d,0x0a
};
static const unsigned int http_202acc_len = 23;

/* HTTP/1.0 400 Bad Request */
static const unsigned char http_400bad[] = {
		0x48,0x54,0x54,0x50,0x2f,0x31,0x2e,0x30,0x20,0x34,0x30,0x30,0x20,0x42,
		0x61,0x64,0x20,0x52,0x65,0x71,0x75,0x65,0x73,0x74,0x0d,0x0a
};
static const unsigned int http_400bad_len = 26;

/* HTTP/1.0 404 File not found */
static const unsigned char http_404fnf[] = {
		0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x30, 0x20, 0x34, 0x30, 0x34,
		0x20, 0x46, 0x69, 0x6c, 0x65, 0x20, 0x6e, 0x6f, 0x74, 0x20, 0x66, 0x6f,
		0x75, 0x6e, 0x64, 0x0d, 0x0a
};
static const unsigned int http_404fnf_len = 29;

/* Server: edyht - based on lwIP */
static const unsigned char http_server[] = {
		0x53, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3a, 0x20, 0x65, 0x64, 0x79, 0x68,
		0x74, 0x20, 0x2d, 0x20, 0x62, 0x61, 0x73, 0x65, 0x64, 0x20, 0x6f, 0x6e,
		0x20, 0x6c, 0x77, 0x49, 0x50, 0x0d,	0x0a
};
static unsigned int http_server_len = 31;

/* "Content-type: text/html */
static const unsigned char http_content_html[] = {
		0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x74, 0x79, 0x70, 0x65,
		0x3a, 0x20, 0x74, 0x65, 0x78, 0x74, 0x2f, 0x68, 0x74, 0x6d, 0x6c, 0x0d,
		0x0a, 0x0d, 0x0a
};
static const unsigned int http_content_html_len = 27;

/* "Content-type: text/csv */
static const unsigned char http_content_csv[] = {
		0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x74, 0x79, 0x70, 0x65,
		0x3a, 0x20, 0x74, 0x65, 0x78, 0x74, 0x2f, 0x63, 0x73, 0x76, 0x0d, 0x0a,
		0x0d, 0x0a
};
static const unsigned int http_content_csv_len = 26;

/* "Content-type: image/png */
static const unsigned char http_content_png[] = {
		0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x74, 0x79, 0x70, 0x65,
		0x3a, 0x20, 0x69, 0x6d,	0x61, 0x67, 0x65, 0x2f, 0x70, 0x6e, 0x67, 0x0d,
		0x0a, 0x0d, 0x0a
};
static const unsigned int http_content_png_len = 27;

/* "Content-type: application/json */
static const unsigned char http_content_json[] = {
		0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x74, 0x79, 0x70, 0x65,
		0x3a, 0x20, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6F,
		0x6e, 0x2f, 0x6a, 0x73, 0x6f, 0x6e, 0x0d, 0x0a, 0x0d, 0x0a
};
static const unsigned int http_content_json_len = 34;

/* "Content-type: text/javascript */
static const unsigned char http_content_js[] = {
		0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x74, 0x79, 0x70, 0x65,
		0x3a, 0x20, 0x74, 0x65, 0x78, 0x74, 0x2f, 0x6a, 0x61, 0x76, 0x61, 0x73,
		0x63, 0x72, 0x69, 0x70, 0x74, 0x0d,	0x0a, 0x0d, 0x0a
};
static const unsigned int http_content_js_len = 33;

/* "Content-type: text/plain */
static const unsigned char http_content_plain[] = {
		0x43,0x6f,0x6e,0x74,0x65,0x6e,0x74,0x2d,0x74,0x79,0x70,0x65,0x3a,0x20,
		0x74,0x65,0x78,0x74,0x2f,0x70,0x6c,0x61,0x69,0x6e,0x0d,0x0a,0x0d,0x0a
};
static const unsigned int http_content_plain_len = 28;

#define ENTRY_LEN   16
#define LIST_LEN    10

typedef struct {
	char name[ENTRY_LEN+1]; //leave one more element for terminating "0"
	char value[ENTRY_LEN+1]; //leave one more element for terminating "0"
} nameVal_t;

static char filename[ENTRY_LEN+1]; //leave one more element for terminating "0"
static nameVal_t queryList[LIST_LEN];


static enum {
	urlState_GET,
	urlState_filename,
	urlState_queryName,
	urlState_queryVal,
} urlState;


static int cntChar;
static int cntElements;  //0 means only filename, otherwise no of query elements
static inline void charProcessInit(void){
	cntChar = 0;
	cntElements = 0;
	urlState = urlState_GET;
}

#define CHARPROC_OK             0
#define CHARPROC_FINISHED       1
#define CHARPROC_ERR_REQUEST   -1
#define CHARPROC_ERR_OWFL      -2
#define CHARPROC_ERR_WRONGCHAR -3

static inline int charProcess(char inChar){

	const char* getStr = "GET /";

	//Process only alphanumeric characters and
	if((inChar >= 0x20) && (inChar <= 0x7e))

		switch(urlState){
		case urlState_GET:
			if(inChar != getStr[cntChar]) return CHARPROC_ERR_REQUEST;
			cntChar++;
			if(cntChar == 5){
				cntChar = 0;
				urlState = urlState_filename;
			}
			break;

		case urlState_filename:
			if(inChar == ' '){
				//Space detected -> end completely
				filename[cntChar] = '\0';
				return CHARPROC_FINISHED;
			}
			if(inChar == '?'){
				//Query detected -> go Query
				filename[cntChar] = '\0';
				cntChar = 0;
				urlState = urlState_queryName;
				break;
			}
			if(cntChar >= ENTRY_LEN) return CHARPROC_ERR_OWFL;
			//possible extension: Maybe tolerate other chars like "_"
			if( ((inChar >= '0') && (inChar <= '9'))
					|| (inChar >= 'A' && inChar <= 'Z')
					|| (inChar >= 'a' && inChar <= 'z')
					|| (inChar == '.')){
				filename[cntChar] = inChar;
				cntChar++;
				break;
			}
			return(CHARPROC_ERR_WRONGCHAR);

		case urlState_queryName:
			if(cntElements >= LIST_LEN) return CHARPROC_ERR_OWFL;
			if(inChar == '='){
				//Value detected -> go value
				queryList[cntElements].name[cntChar] = '\0';
				cntChar = 0;
				urlState = urlState_queryVal;
				break;
			}
			if(cntChar >= ENTRY_LEN) return CHARPROC_ERR_OWFL;
			if( ((inChar >= '0') && (inChar <= '9'))
					|| (inChar >= 'A' && inChar <= 'Z')
					|| (inChar >= 'a' && inChar <= 'z')
					|| (inChar == '.')
					|| (inChar == '_' )){
				queryList[cntElements].name[cntChar] = inChar;
				cntChar++;
				break;
			}
			return(CHARPROC_ERR_WRONGCHAR);

		case urlState_queryVal:
			if(inChar == ' '){
				//Space detected -> end completely
				queryList[cntElements].value[cntChar] = '\0';
				cntElements++;
				return CHARPROC_FINISHED;
			}
			if(inChar == '&'){
				//Next token detected -> go Name
				queryList[cntElements].value[cntChar] = '\0';
				cntChar = 0;
				cntElements++;
				urlState = urlState_queryName;
				break;
			}
			if(cntChar >= ENTRY_LEN) return CHARPROC_ERR_OWFL;
			//possible extension:  Maybe tolerate other chars like "_"
			if( ((inChar >= '0') && (inChar <= '9'))
					|| (inChar >= 'A' && inChar <= 'Z')
					|| (inChar >= 'a' && inChar <= 'z')
					|| (inChar == '.')
					|| (inChar == '-')){
				queryList[cntElements].value[cntChar] = inChar;
				cntChar++;
				break;
			}
			if(inChar == '+'){
				queryList[cntElements].value[cntChar] = ' ';
				cntChar++;
				break;
			}
			return(CHARPROC_ERR_WRONGCHAR);
		}

	return(CHARPROC_OK);
}

static int array[1000];
static void arrayProcess(struct netconn *conn){
	char val[20];
	memset(val, 0,20);

	for(int pos = 0; pos<1000; pos++){
		array[pos] = pos/2 + 1 + pos/3; //fill some "random" data to array
		if(pos == 0){
			sprintf(val, "%d", array[pos]);
		}
		else{
			sprintf(val, ",%d", array[pos]);
		}
		netconn_write(conn, val, strlen(val), NETCONN_COPY);
	}
}

static void queryShow(struct netconn *conn){

	char line[200];
	memset(line, 0,200);

	sprintf(line, "Number of elements: %d\n", cntElements);
	netconn_write(conn, line, strlen(line), NETCONN_COPY);

	sprintf(line, "<table>\n");
	netconn_write(conn, line, strlen(line), NETCONN_COPY);

	int i;
	for(i=0;i<cntElements;i++){
		sprintf(line, "<tr><td>%s <td>%s\n", queryList[i].name, queryList[i].value);
		netconn_write(conn, line, strlen(line), NETCONN_COPY);
	}
	sprintf(line, "</table>\n");
	netconn_write(conn, line, strlen(line), NETCONN_COPY);
}


static inline void webpageProcess(struct netconn *conn){

	if((strncmp(filename, "", ENTRY_LEN) == 0) || (strncmp(filename, "index.htm", ENTRY_LEN) == 0))
	{
		netconn_write(conn, http_200ok, http_200ok_len, NETCONN_NOCOPY);
		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
		netconn_write(conn, http_content_html, http_content_html_len, NETCONN_NOCOPY);
		netconn_write(conn, htdocs_index_htm, htdocs_index_htm_len, NETCONN_NOCOPY);
	}
	else if(strncmp(filename, "credits.htm", ENTRY_LEN) == 0)
	{
		netconn_write(conn, http_200ok, http_200ok_len, NETCONN_NOCOPY);
		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
		netconn_write(conn, http_content_html, http_content_html_len, NETCONN_NOCOPY);
		netconn_write(conn, htdocs_credits_htm, htdocs_credits_htm_len, NETCONN_NOCOPY);
	}
	else if(strncmp(filename, "tasks.htm", ENTRY_LEN) == 0)
	{
		netconn_write(conn, http_200ok, http_200ok_len, NETCONN_NOCOPY);
		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
		netconn_write(conn, http_content_html, http_content_html_len, NETCONN_NOCOPY);
		netconn_write(conn, htdocs_tasks_begin_htm, htdocs_tasks_begin_htm_len, NETCONN_NOCOPY);
		/* Load dynamic page part */
		page_FreeRTOS_Tasks(conn);
		netconn_write(conn, htdocs_tasks_end_htm, htdocs_tasks_end_htm_len, NETCONN_NOCOPY);

	}
	else if(strncmp(filename, "lwip.htm", ENTRY_LEN) == 0)
	{
		netconn_write(conn, http_200ok, http_200ok_len, NETCONN_NOCOPY);
		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
		netconn_write(conn, http_content_html, http_content_html_len, NETCONN_NOCOPY);
		netconn_write(conn, htdocs_lwip_begin_htm, htdocs_lwip_begin_htm_len, NETCONN_NOCOPY);
		/* Load dynamic page part */
		//page_LwIP_Info(conn);
		netconn_write(conn, htdocs_lwip_end_htm, htdocs_lwip_end_htm_len, NETCONN_NOCOPY);
	}
	else if(strncmp(filename, "testform.htm", ENTRY_LEN) == 0)
	{
		netconn_write(conn, http_200ok, http_200ok_len, NETCONN_NOCOPY);
		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
		netconn_write(conn, http_content_html, http_content_html_len, NETCONN_NOCOPY);
		netconn_write(conn, htdocs_testform_begin_htm, htdocs_testform_begin_htm_len, NETCONN_NOCOPY);
		queryShow(conn);
		netconn_write(conn, htdocs_testform_end_htm, htdocs_testform_end_htm_len, NETCONN_NOCOPY);
	}
	else if(strncmp(filename, "test.json", ENTRY_LEN) == 0)
	{
		netconn_write(conn, http_200ok, http_200ok_len, NETCONN_NOCOPY);
		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
		netconn_write(conn, http_content_json, http_content_json_len, NETCONN_NOCOPY);
		char *begin_json_array = "{\n\"val\":[";
		netconn_write(conn, begin_json_array, strlen(begin_json_array), NETCONN_NOCOPY);
		arrayProcess(conn);
		char *end_json_array = "]\n}";
		netconn_write(conn, end_json_array, strlen(end_json_array), NETCONN_NOCOPY);
	}
	//  favicon.ico might be automatically fetched by some browsers, e.g. firefox
	//	else if(strncmp(filename, "favicon.png", ENTRY_LEN) == 0)
	//	{
	//		netconn_write(conn, http_200ok, http_200ok_len, NETCONN_NOCOPY);
	//		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
	//		netconn_write(conn, http_content_png, http_content_png_len, NETCONN_NOCOPY);
	//		netconn_write(conn, htdocs_favicon_png, htdocs_favicon_png_len, NETCONN_NOCOPY);
	//	}
	else
	{
		/* Show error page */
		netconn_write(conn, http_404fnf, http_404fnf_len, NETCONN_NOCOPY);
		netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
		netconn_write(conn, http_content_html, http_content_html_len, NETCONN_NOCOPY);
		netconn_write(conn, htdocs_err404_htm, htdocs_err404_htm_len, NETCONN_NOCOPY);
	}
}

static inline void webpageBadProcess(struct netconn *conn){
	netconn_write(conn, http_400bad, http_400bad_len, NETCONN_NOCOPY);
	netconn_write(conn, http_server, http_server_len, NETCONN_NOCOPY);
	//improve: Add some html info
	netconn_write(conn, http_content_plain, http_content_plain_len, NETCONN_NOCOPY);
	netconn_write(conn, "ERR\n", 4, NETCONN_NOCOPY);
}

static void serve_get_request(struct netconn *conn)
{
	struct netbuf *inbuf;
	err_t recv_err;
	char* buf;
	u16_t buflen;
	int doexit = 0;
	char myChar;

	//Set timeout
	netconn_set_recvtimeout ( conn, 2000 );

	charProcessInit();

	do{
		// Receive data
		recv_err = netconn_recv(conn, &inbuf);

		if (recv_err == ERR_OK)	{
			if (netconn_err(conn) == ERR_OK) {
				do {
					//Get data from netbuf
					netbuf_data(inbuf, (void**)&buf, &buflen);

					int i;
					//process data byte by byte
					for(i=0; i<buflen; i++){
						myChar = buf[i];

						int ret = charProcess(myChar);

						if(ret < 0) {
							//Error!
							webpageBadProcess(conn);
							doexit = 4;
							break;
						};

						if(ret == 1){
							//Process Webpage
							webpageProcess(conn);
							//Exit regularly
							doexit = 100;
							break;
						}

					} //for(i=0; i<buflen; i++){
				} while((netbuf_next(inbuf) >= 0) && (doexit==0));
			} //if (netconn_err(conn) == ERR_OK)
			else {
				doexit = 3;
			}
		} //if (recv_err == ERR_OK)
		else {
			doexit = 2;
		}

		// delete buffer)
		netbuf_delete(inbuf);

	} while(doexit == 0);


	// close connection
	netconn_close(conn);
}


static void edyht_thread(void *arg)
{ 
	struct netconn *conn, *newconn;
	err_t err, accept_err;

	conn = netconn_new(NETCONN_TCP);

	if (conn!= NULL)
	{
		//bind (http port)
		err = netconn_bind(conn, NULL, 80);

		if (err == ERR_OK)
		{
			netconn_listen(conn);
			while(1)
			{
				//wait for incoming connection
				accept_err = netconn_accept(conn, &newconn);
				if(accept_err == ERR_OK)
				{
					//serve request
					serve_get_request(newconn);
					netconn_delete(newconn);
				}
			}
		}
		else
		{
			//improvement: Notify error!
			netconn_delete(newconn);
		}
	}
	else
	{
		//improvement: Notify error!
	}
	for(;;); //send thread to endless loop; should not happen!
}

void edyht_init()
{
	sys_thread_new("edyht", edyht_thread, NULL, 2500, EDYHT_PRIO);
}

static void page_FreeRTOS_Tasks(struct netconn *conn)
{
	portCHAR buffer[1000];
	time_t myTime;

	netconn_write(conn, "<pre>\r\n",7,NETCONN_COPY);
	netconn_write(conn, "Name          State  Priority  Stack   Num\r\n", 44, NETCONN_COPY);
	netconn_write(conn, "------------------------------------------\r\n", 44, NETCONN_COPY);

	//Create Task List
	memset(buffer, 0,1000);
	vTaskList(buffer);
	//vTaskGetRunTimeStats(buffer);
	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);

	netconn_write(conn, "------------------------------------------\r\n", 44, NETCONN_COPY);
	netconn_write(conn, "System Time: ", 13, NETCONN_COPY);

	time(&myTime);
	memset(buffer, 0,1000);
	ctime_r(&myTime, buffer );
	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);

	netconn_write(conn, "</pre>\r\n", 8, NETCONN_COPY);
}

//void lwip_display_toString(struct stats_proto *proto, const char *name, char* string)
//{
//	char tmp[100];
//	sprintf(string, "\n%s\n\t", name);
//	sprintf(tmp, "xmit: %d\n\t", proto->xmit);
//	strcat(string, tmp);
//	sprintf(tmp, "recv: %d\n\t", proto->recv);
//	strcat(string, tmp);
//	sprintf(tmp, "fw: %d\n\t", proto->fw);
//	strcat(string, tmp);
//	sprintf(tmp, "drop: %d\n\t", proto->drop);
//	strcat(string, tmp);
//	sprintf(tmp, "chkerr: %d\n\t", proto->chkerr);
//	strcat(string, tmp);
//	sprintf(tmp, "lenerr: %d\n\t", proto->lenerr);
//	strcat(string, tmp);
//	sprintf(tmp, "memerr: %d\n\t", proto->memerr);
//	strcat(string, tmp);
//	sprintf(tmp, "rterr: %d\n\t", proto->rterr);
//	strcat(string, tmp);
//	sprintf(tmp, "proterr: %d\n\t", proto->proterr);
//	strcat(string, tmp);
//	sprintf(tmp, "opterr: %d\n\t", proto->opterr);
//	strcat(string, tmp);
//	sprintf(tmp, "err: %d\n\t", proto->err);
//	strcat(string, tmp);
//	sprintf(tmp, "cachehit: %d\n", proto->cachehit);
//	strcat(string, tmp);
//}
//
//static void lwip_display_mem_toString(struct stats_mem *mem, const char *name, char* string)
//{
//	char tmp[100];
//	sprintf(string, "\nMEM %s\n\t", name);
//	sprintf(tmp, "avail: %lu\n\t", (u32_t)mem->avail);
//	strcat(string, tmp);
//	sprintf(tmp, "used: %lu\n\t", (u32_t)mem->used);
//	strcat(string, tmp);
//	sprintf(tmp, "max: %lu\n\t", (u32_t)mem->max);
//	strcat(string, tmp);
//	sprintf(tmp, "err: %lu\n", (u32_t)mem->err);
//	strcat(string, tmp);
//}
//
//static void lwip_display_sys_toString(struct stats_sys *sys, char* string)
//{
//	char tmp[100];
//	sprintf(string, "\nSYS\n\t");
//	sprintf(tmp, "sem.used:  %lu\n\t", (u32_t)sys->sem.used);
//	strcat(string, tmp);
//	sprintf(tmp, "sem.max:   %lu\n\t", (u32_t)sys->sem.max);
//	strcat(string, tmp);
//	sprintf(tmp, "sem.err:   %lu\n\t", (u32_t)sys->sem.err);
//	strcat(string, tmp);
//	sprintf(tmp, "mutex.used: %lu\n\t", (u32_t)sys->mutex.used);
//	strcat(string, tmp);
//	sprintf(tmp, "mutex.max:  %lu\n\t", (u32_t)sys->mutex.max);
//	strcat(string, tmp);
//	sprintf(tmp, "mutex.err:  %lu\n\t", (u32_t)sys->mutex.err);
//	strcat(string, tmp);
//	sprintf(tmp, "mbox.used:  %lu\n\t", (u32_t)sys->mbox.used);
//	strcat(string, tmp);
//	sprintf(tmp, "mbox.max:   %lu\n\t", (u32_t)sys->mbox.max);
//	strcat(string, tmp);
//	sprintf(tmp, "mbox.err:   %lu\n\t", (u32_t)sys->mbox.err);
//	strcat(string, tmp);
//}
//
//
//static void page_LwIP_Info(struct netconn *conn)
//{
//	portCHAR buffer[1000];
//
//	netconn_write(conn, "<pre>\r\n",7,NETCONN_COPY);
//	netconn_write(conn, "Test lwIP Stats:                          \r\n", 44, NETCONN_COPY);
//	netconn_write(conn, "------------------------------------------\r\n", 44, NETCONN_COPY);
//
//	memset(buffer, 0,1000);
//
//	lwip_display_toString(&lwip_stats.link, "LINK", buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	lwip_display_toString(&lwip_stats.ip, "IP", buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	lwip_display_toString(&lwip_stats.icmp, "ICMP", buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	lwip_display_toString(&lwip_stats.tcp, "TCP", buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	lwip_display_toString(&lwip_stats.udp, "UDP", buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	lwip_display_toString(&lwip_stats.etharp, "ETHARP", buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	lwip_display_mem_toString(&lwip_stats.mem, "HEAP", buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	lwip_display_sys_toString(&lwip_stats.sys, buffer);
//	netconn_write(conn, buffer, strlen(buffer), NETCONN_COPY);
//
//	netconn_write(conn, "</pre>\r\n", 8, NETCONN_COPY);
//}
