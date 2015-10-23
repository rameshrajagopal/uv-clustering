#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "utils.h"
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#define  SERVER_ADDRESS  "127.0.0.1"
#define  SERVER_PORT  7000
#define  MAX_REQUESTS  (2)

static uv_loop_t * loop;

void on_connect(uv_connect_t *req, int status);
void on_write_end(uv_write_t *req, int status);
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t * buf);
void echo_read(uv_stream_t *server, ssize_t nread, const uv_buf_t * buf);

void echo_read(uv_stream_t * server, ssize_t nread, const uv_buf_t * buf)
{
    if (nread < 0) {
        DBG_PRINT_ERR("error on write end");
        return;
    }
    struct timeval curtime;
    gettimeofday(&curtime, NULL);
    DBG_LOG("Response: %s sec:%ld u_sec: %ld\n", 
            buf->base, curtime.tv_sec, curtime.tv_usec);
}

void alloc_buffer(uv_handle_t * handle, size_t size, uv_buf_t * buf)
{
    *buf = uv_buf_init((char *) malloc(size), size);
}

void on_write_end(uv_write_t * req, int status)
{
    if (status == -1) {
        DBG_PRINT_ERR("error on write end");
        return;
    }
    uv_read_start(req->handle, alloc_buffer, echo_read);
}

void client_request_cb(uv_work_t * req)
{
    struct request * c_req = (struct request *)req->data;

    DBG();
    char data[1024];
    for (int num = 0; num < c_req->nrequests; ++num) {
       struct timeval curtime;
       gettimeofday(&curtime, NULL);
       DBG_LOG("Request: %d:%d sec:%ld usec:%ld\n", c_req->client_req_num, num, curtime.tv_sec, curtime.tv_usec);
       snprintf(data, sizeof(data), "%d:%d", c_req->client_req_num, num); 
       uv_buf_t buf = uv_buf_init(data, sizeof(data));
       int buf_count = 1;
       uv_write_t write_req;
       uv_write(&write_req, (uv_stream_t *)c_req->handle, &buf, buf_count, on_write_end);
    }
}
void client_request_cleanup_cb(uv_work_t * req, int status)
{
    struct request * c_req = (struct request *)req->data;
    DBG();
    free(c_req);
}

void on_connect(uv_connect_t * req, int status)
{
    if (status == -1) {
        fprintf(stderr, "error on connect");
        return;
    }
    struct request * c_req = malloc(sizeof(struct request));
    assert(c_req != NULL);
    c_req->req.data = (void *) c_req;
    c_req->handle = (uv_handle_t *)req->handle;
    c_req->nrequests = MAX_REQUESTS;
    uv_queue_work(uv_default_loop(), &c_req->req, client_request_cb, client_request_cleanup_cb);
}

int main(void)
{
    loop = uv_default_loop();

    uv_tcp_t client;

    uv_tcp_init(loop, &client);

    struct sockaddr_in req_addr;
    uv_ip4_addr(SERVER_ADDRESS, SERVER_PORT, &req_addr);
    uv_connect_t connect_req;

    uv_tcp_connect(&connect_req, &client, (const struct sockaddr *)&req_addr, on_connect);

    return uv_run(loop, UV_RUN_DEFAULT);
}

