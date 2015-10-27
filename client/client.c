#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include <uv.h>
#include "utils.h"

#define  MAX_REQUESTS  (10)
#define  MAX_CLIENTS (1)

static uv_loop_t * loop;


void on_connect(uv_connect_t *req, int status);
void on_write_end(uv_write_t *req, int status);
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t * buf);
void data_read_cb(uv_stream_t *server, ssize_t nread, const uv_buf_t * buf);

void data_read_cb(uv_stream_t * server, ssize_t nread, const uv_buf_t * buf)
{
    DBG_PRINT("%s: nread:%ld\n", __FUNCTION__, nread);
    if (nread < 0) {
        DBG_PRINT_ERR("error on write end");
        return;
    }
    struct timeval curtime;
    gettimeofday(&curtime, NULL);
    DBG_LOG("Response: %s sec:%ld u_sec: %ld data read: %ld\n", 
            buf->base, curtime.tv_sec, curtime.tv_usec, nread);
}

void alloc_buffer(uv_handle_t * handle, size_t size, uv_buf_t * buf)
{
    DBG();
    *buf = uv_buf_init((char *) malloc(size), size);
}

void data_write(write_req_t * wr)
{
    int buf_count = 1;
    if (wr->req_cnt < MAX_REQUESTS) {
        ++wr->req_cnt;
        uv_write(&wr->req, wr->handle, &wr->buf, buf_count, on_write_end);
    }
}

void on_write_end(uv_write_t * req, int status)
{
    write_req_t * wr = (write_req_t *) req;
    DBG_LOG("%s: handle: %p %p\n", __FUNCTION__, wr, wr->handle);
    if (status == -1) {
        DBG_PRINT_ERR("error on write end");
        return;
    }
   uv_read_start(wr->req.handle, alloc_buffer, data_read_cb);
   data_write(wr);
}

void client_request_cb(uv_work_t * req)
{
    struct request * c_req = (struct request *)req->data;

    DBG();
    for (int num = 0; num < c_req->nrequests; ++num) {
       struct timeval curtime;
       gettimeofday(&curtime, NULL);
       DBG_LOG("Request: %d:%d sec:%ld usec:%ld\n", c_req->client_req_num, num, curtime.tv_sec, curtime.tv_usec);
       uv_buf_t buf = uv_buf_init(malloc(sizeof(char) * 1024), 1024);
       snprintf(buf.base, buf.len, "%d:%d", c_req->client_req_num, num); 
       int buf_count = 1;
       uv_write_t * wr = malloc(sizeof(uv_write_t));
       assert(wr != NULL);
       DBG_PRINT("%s: handle: %p\n", __FUNCTION__, wr);
       uv_write(wr, (uv_stream_t *)c_req->handle, &buf, buf_count, on_write_end);
       usleep(400 * 1000); //400 millisec
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
    static int client_req_num = 0;
    if (status == -1) {
        fprintf(stderr, "error on connect");
        return;
    }
    DBG_LOG("connected: %d\n", client_req_num++);
#if 0
    struct request * c_req = malloc(sizeof(struct request));
    assert(c_req != NULL);
    c_req->req.data = (void *) c_req;
    c_req->handle = (uv_handle_t *)req->handle;
    c_req->nrequests = MAX_REQUESTS;
    uv_queue_work(uv_default_loop(), &c_req->req, client_request_cb, client_request_cleanup_cb);
#endif
    write_req_t * wr = malloc(sizeof(write_req_t));
    assert(wr != NULL);
    wr->req_cnt = 0;
    wr->handle = (uv_stream_t *) req->handle;

    int buf_count = 1;
    wr->buf = uv_buf_init(malloc(sizeof(char) * 1024), 1024);
    snprintf(wr->buf.base, wr->buf.len, "%d:%d", ++client_req_num, wr->req_cnt);

    DBG_LOG("%s wr: %p handle: %p\n", __FUNCTION__, wr, req->handle);
    uv_write(&wr->req, (uv_stream_t *)req->handle, &wr->buf, buf_count, on_write_end);
}

void client_task(void * arg)
{
    int client_num = (int) (unsigned long)arg;

    DBG_LOG("Client started... %d\n", client_num);
    loop = uv_default_loop();

    uv_tcp_t client;

    uv_tcp_init(loop, &client);

    struct sockaddr_in req_addr;
    uv_ip4_addr(SERVER_ADDRESS, SERVER_PORT, &req_addr);
    uv_connect_t connect_req;

    uv_tcp_connect(&connect_req, &client, (const struct sockaddr *)&req_addr, on_connect);
    (void)uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}

int main(void)
{
    uv_thread_t threads[MAX_CLIENTS];
    for (int num = 0; num < MAX_CLIENTS; ++num) {
        uv_thread_create(&threads[num], client_task, (void *)(unsigned long)num);
    }
    for (int num = 0; num < MAX_CLIENTS; ++num) {
        uv_thread_join(&threads[num]);
    }
    return 0;
}

