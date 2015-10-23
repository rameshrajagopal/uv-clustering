#ifndef _UTILS_H_INCLUDED_
#define _UTILS_H_INCLUDED_

#define  WORKER_ADDRESS  "127.0.0.1"
#define  WORKER_PORT  7000

#define DBG()  printf("%s:%d\n", __FUNCTION__, __LINE__)
#define DBG_PRINT(fmt...) printf(fmt)
#define DBG_PRINT_ERR(fmt...) printf(fmt)
#define DBG_PRINT_INFO(fmt...) printf(fmt)
#define DBG_LOG(fmt...) printf(fmt)

typedef void (*on_connection_callback)(uv_stream_t *, int);

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

struct request 
{
    uv_work_t req;
    int client_req_num;
    uv_handle_t * handle;
    const uv_buf_t * buf;
    ssize_t nread;
    int nrequests;
};

#endif /*_UTILS_H_INCLUDED_*/
