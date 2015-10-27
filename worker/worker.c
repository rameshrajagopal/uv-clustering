#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include "uv.h"
#include "utils.h"

#define MAX_NUM_THREADS  (10)

//static uv_loop_t * work_queue_loop;
static queue_t * main_queue;

static uv_buf_t * get_new_buffer(char * base, int len);
static void on_close_cb(uv_handle_t* handle)
{
  DBG();
  free(handle);
}

static void after_shutdown_cb(uv_shutdown_t* req, int status) {
  /*assert(status == 0);*/
  if (status < 0)
    DBG_PRINT_ERR("err: %s\n", uv_strerror(status));
  uv_close((uv_handle_t*)req->handle, on_close_cb);
  free(req);
}


static void after_write_cb(uv_write_t* req, int status) 
{
  write_req_t * wr = (write_req_t*)req;

  DBG_PRINT("%s:  wr: %p buf->base: %p\n", __FUNCTION__, wr, wr->buf.base);
  if (wr->buf.base != NULL)
    free(wr->buf.base);
  free(wr);
  if (status == 0)
    return;
  DBG_PRINT_ERR("uv_write : %s\n", uv_strerror(status));
  if (status == UV_ECANCELED)
    return;
  assert(status == UV_EPIPE);
  uv_close((uv_handle_t*)req->handle, on_close_cb);
}

write_req_t * new_request(char * base, unsigned len, uv_stream_t * handle)
{
    write_req_t * new = malloc(sizeof(write_req_t));
    assert(new != NULL);
    new->buf = uv_buf_init(base, len);
    new->handle = handle;
    return new;
}

void work_request_cb(uv_work_t * req)
{
    work_request_t * reqdata = (work_request_t *)req->data;
    worker_data_t * wdata = reqdata->wdata;
    int total_length = 0;
    uv_buf_t * buf = NULL;
    int curpos = 0;
    write_req_t * wr = NULL;
    uint8_t temp[1024];
    int temp_length = 0;

    while (!is_empty(wdata->q)) {
        curpos = 0;
        if (total_length > 0 && buf) {
            total_length = 0;
            free(buf->base);
            free(buf);
        }
        buf = (uv_buf_t *)queue_pop(wdata->q);
        total_length += buf->len + temp_length;
        if (total_length > 1024) {
            wr = new_request(malloc(1024), 1024, wdata->handle);
            assert(wr != NULL);
            memcpy(wr->buf.base, temp, temp_length);
            memcpy(wr->buf.base + temp_length, buf->base + curpos, 1024 - temp_length);
            queue_push(main_queue, wr);
     //       uv_write(&wr->req, wr->handle, &wr->buf, 1, after_write_cb);
            total_length -= 1024;
            curpos += (1024 - temp_length);
        }
        while (total_length >= 1024) {
            wr = new_request(malloc(1024), 1024, wdata->handle);
            assert(wr != NULL);
            memcpy(wr->buf.base, buf->base + curpos, 1024);
            queue_push(main_queue, wr); 
            //uv_write(&wr->req, wr->handle, &wr->buf, 1, after_write_cb);
            total_length -= 1024;
            curpos += 1024;
        }
        if (total_length > 0) {
           memcpy(temp, buf->base+curpos, total_length);
           temp_length = total_length;
        }
    }
    if (total_length > 0) {
        uv_buf_t * new = get_new_buffer(malloc(total_length), total_length);
        assert(new != NULL);
        memcpy(new->base, buf->base + curpos, total_length);
        queue_push_front(wdata->q, new);
        free(buf);
    }
}

void work_request_cleanup_cb(uv_work_t * req, int status)
{
    DBG_PRINT("%s: ", __FUNCTION__);
    free(req);
}

static uv_buf_t * get_new_buffer(char * base, int len)
{
    uv_buf_t * new = malloc(sizeof(uv_buf_t));
    assert(new != NULL);
    new->base = base;
    new->len  = len;
    return new;
}


static void after_read_cb(uv_stream_t * handle,
                       ssize_t nread,
                       const uv_buf_t * buf) 
{
  DBG();
  if (nread < 0) {
      /*assert(nread == UV_EOF);*/
      if (buf->base != NULL) free(buf->base);
      DBG_PRINT_ERR("err: %s\n", uv_strerror(nread));

      uv_shutdown_t * req = (uv_shutdown_t*) malloc(sizeof(*req));
      assert(req != NULL);

      int r = uv_shutdown(req, handle, after_shutdown_cb);
      assert(r == 0);
      return;
  }
  DBG_PRINT("Data received of length: %p %ld %ld %p\n", buf->base, buf->len, nread, handle);
  if (nread == 0) {
      DBG_PRINT("%s:%d length: %ld\n", __FUNCTION__, __LINE__, nread);
      return;
  }
  /* queue up the buffer */
  worker_data_t * wdata = (worker_data_t *) handle;
  wdata->handle = handle; //&wdata->client;
#if 0
  write_req_t * wr = new_request(malloc(1024), 1024, wdata->handle);
  assert(wr != NULL);
  //memcpy(wr->buf.base, buf->base + curpos, 1024);
  DBG_PRINT("%s:%d %d\n", __FUNCTION__, __LINE__, ((uv_stream_t *)wr->handle)->type);
  uv_write(&wr->req, wr->handle, &wr->buf, 1, after_write_cb);
#else
  queue_push(wdata->q, get_new_buffer(buf->base, nread));
  /* 
   * divide the buffer into  requests and pushing into queue will be done by
   * work queue
   */
  work_request_t * wr = malloc(sizeof(work_request_t));
  assert(wr != NULL);
  wr->req.data = (void *) wr;
  wr->wdata = wdata;
  uv_queue_work(uv_default_loop(), (uv_work_t *)wr, work_request_cb, work_request_cleanup_cb);
#endif
}

static void alloc_cb(uv_handle_t * handle,
                     size_t suggested_size,
                     uv_buf_t* buf) 
{
  DBG();
  buf->base = malloc(suggested_size);
  assert(buf->base != NULL);
  DBG_PRINT("%s:%d handle: %p base: %p %ld\n", __FUNCTION__, __LINE__, handle, buf->base, suggested_size);
  buf->len = suggested_size;
}

static void on_connection_cb(uv_stream_t * server, int status)
{
  int r;
  worker_data_t * worker = malloc(sizeof(worker_data_t));

  assert(worker != NULL);
  assert(status == 0);
  worker->q = queue_init();

  r = uv_tcp_init(uv_default_loop(), &worker->client);
  assert(r == 0);

  worker->client.data = server;

  r = uv_accept(server, (uv_stream_t *)&worker->client);
  assert(r == 0);
  printf("on connection: client: %p\n", &worker->client);
  /* each connection create a client structure */
  r = uv_read_start((uv_stream_t *)&worker->client, alloc_cb, after_read_cb);
  assert(r == 0);
}

static int tcp_server_init(const char * serv_addr, int port, on_connection_callback  connection)
{
  uv_tcp_t * tcp_server;
  struct sockaddr_in addr;
  int r;

  r = uv_ip4_addr(serv_addr, port, &addr);
  assert(r == 0);

  tcp_server = (uv_tcp_t*) malloc(sizeof(*tcp_server));
  assert(tcp_server != NULL);

  r = uv_tcp_init(uv_default_loop(), tcp_server);
  assert(r == 0);

  r = uv_tcp_bind(tcp_server, (const struct sockaddr*)&addr, 0);
  assert(r == 0);

  r = uv_listen((uv_stream_t*)tcp_server, SOMAXCONN, connection);
  assert(r == 0);

  return 0;
}

void worker_task(void * arg)
{
    queue_t * q = arg;

    while (1) {
        write_req_t * wr = (write_req_t *)queue_pop(q);
        /* do actual work */
        usleep(100 * 1000);
        DBG_PRINT("writing response back: %p\n", wr->handle);
        uv_write(&wr->req, wr->handle, &wr->buf, 1, after_write_cb);
    }
}


int main(int argc, char * argv[]) 
{
  uv_thread_t tid[MAX_NUM_THREADS];
  
  main_queue = queue_init();
  assert(main_queue != NULL);

  for (int num = 0; num < MAX_NUM_THREADS; ++num) {
      uv_thread_create(&tid[num], worker_task, main_queue); 
  }
#if 0
  work_queue_loop = malloc(sizeof(uv_loop_t));
  assert(work_queue_loop != NULL);
  uv_loop_init(work_queue_loop);
#endif
  
  int r = tcp_server_init(WORKER_ADDRESS, WORKER_PORT, on_connection_cb);
  assert(r == 0);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  assert(r == 0);

  return 0;
}
