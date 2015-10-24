#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include "uv.h"
#include "utils.h"

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
#if 1
  if (wr->buf.base != NULL)
    free(wr->buf.base);
#endif
  free(wr);
  if (status == 0)
    return;
  DBG_PRINT_ERR("uv_write : %s\n", uv_strerror(status));
  if (status == UV_ECANCELED)
    return;
  assert(status == UV_EPIPE);
  uv_close((uv_handle_t*)req->handle, on_close_cb);
}

void work_request_cb(uv_work_t * req)
{
    struct request * c_req = (struct request *)req->data;

    DBG_PRINT_INFO("%s: Received work: crn: %d datalength: %ld\n",
                     __FUNCTION__, c_req->client_req_num, c_req->nread);
    /* do actual work */
    usleep(100 * 1000);
    write_req_t * wr;
    wr = (write_req_t *) malloc(sizeof(*wr));
    assert(wr != NULL);
    DBG_PRINT("%s:  wr: %p buf->base: %p\n", __FUNCTION__, wr, c_req->buf->base);
    wr->buf = uv_buf_init(c_req->buf->base, c_req->nread);
    int r = uv_write(&wr->req, (uv_stream_t *)c_req->handle, &wr->buf, 1, after_write_cb);
    assert(r == 0);
}

void work_request_cleanup_cb(uv_work_t * req, int status)
{
    struct request * c_req = (struct request *)req->data;
    DBG();
    free(c_req);
}

static void after_read_cb(uv_stream_t * handle,
                       ssize_t nread,
                       const uv_buf_t * buf) 
{
  int r;
  uv_shutdown_t* req;
  static int client_req_num = 0;/* make it atomic */

  DBG();
  if (nread < 0) {
      /*assert(nread == UV_EOF);*/
      if (buf->base != NULL) free(buf->base);
      DBG_PRINT_ERR("err: %s\n", uv_strerror(nread));

      req = (uv_shutdown_t*) malloc(sizeof(*req));
      assert(req != NULL);

      r = uv_shutdown(req, handle, after_shutdown_cb);
      assert(r == 0);
      return;
  }
  if (nread == 0) {
      DBG_PRINT("%s:%d length: %ld\n", __FUNCTION__, __LINE__, nread);
      return;
  }
  /* generate ReqNum here and map ReqNum -> handle */
  struct request * c_req = malloc(sizeof(struct request));
  assert(c_req != NULL);
  c_req->req.data = (void *)c_req;
  c_req->client_req_num = client_req_num++;
  c_req->handle = (uv_handle_t *)handle;
  c_req->buf = buf;
  c_req->nread = nread;
  uv_queue_work(uv_default_loop(), &c_req->req, work_request_cb, work_request_cleanup_cb);
}

static void alloc_cb(uv_handle_t* handle,
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
  uv_tcp_t* client;
  int r;

  assert(status == 0);

  client = malloc(sizeof(uv_tcp_t));
  assert(client != NULL);

  r = uv_tcp_init(uv_default_loop(), client);
  assert(r == 0);

  client->data = server;

  r = uv_accept(server, (uv_stream_t*)client);
  assert(r == 0);
  printf("on connection: client: %p\n", client);
  r = uv_read_start((uv_stream_t*)client, alloc_cb, after_read_cb);
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


int main(int argc, char * argv[]) 
{

  int r = tcp_server_init(WORKER_ADDRESS, WORKER_PORT, on_connection_cb);
  assert(r == 0);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  assert(r == 0);

  return 0;
}
