#ifndef SRC_NODE_HTTP2_CORE_H_
#define SRC_NODE_HTTP2_CORE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "util.h"
#include "util-inl.h"
#include "uv.h"
#include "nghttp2/nghttp2.h"

#include <map>
#include <memory>
#include <string>

namespace node {
namespace http2 {

typedef struct nghttp2_session_s nghttp2_session_t;
typedef struct nghttp2_stream_s nghttp2_stream_t;
typedef struct nghttp2_stream_write_s nghttp2_stream_write_t;
typedef struct nghttp2_data_chunk_s nghttp2_data_chunk_t;
typedef struct nghttp2_data_chunks_s nghttp2_data_chunks_t;

#define MAX_BUFFER_COUNT 10
#define SEND_BUFFER_RECOMMENDED_SIZE 4096

typedef enum {
  NGHTTP2_SESSION_SERVER,
  NGHTTP2_SESSION_CLIENT
} nghttp2_session_type;

typedef enum {
  NGHTTP2_SHUTDOWN_FLAG_GRACEFUL,
  NGHTTP2_SHUTDOWN_FLAG_IMMEDIATE
} nghttp2_shutdown_flags;

typedef enum {
  NGHTTP2_STREAM_FLAG_NONE = 0x0,
  // Writable side has ended
  NGHTTP2_STREAM_FLAG_SHUT = 0x1,
  // Readable side has ended
  NGHTTP2_STREAM_FLAG_ENDED = 0x2
} nghttp2_stream_flags;


// Callbacks
typedef void (*nghttp2_stream_alloc_cb)(
    std::shared_ptr<nghttp2_stream_t> handle,
    size_t suggested_size,
    const uv_buf_t* buf);
typedef void (*nghttp2_stream_read_cb)(
    std::shared_ptr<nghttp2_stream_t> stream,
    ssize_t nread,
    const uv_buf_t* buf);
typedef void (*nghttp2_stream_write_cb)(
    nghttp2_stream_write_t* req,
    int status);


struct nghttp2_stream_write_queue {
  unsigned int nbufs = 0;
  nghttp2_stream_write_t* req = nullptr;
  nghttp2_stream_write_cb cb = nullptr;
  nghttp2_stream_write_queue* next = nullptr;
  MaybeStackBuffer<uv_buf_t, MAX_BUFFER_COUNT> bufs;
};

struct nghttp2_header_list {
  nghttp2_rcbuf* name = nullptr;
  nghttp2_rcbuf* value = nullptr;
  nghttp2_header_list* next = nullptr;
};

typedef enum {
  NGHTTP2_CB_NONE,
  NGHTTP2_CB_SESSION_SEND,
  NGHTTP2_CB_HEADERS,
  NGHTTP2_CB_STREAM_CLOSE,
  NGHTTP2_CB_DATA_CHUNKS,
  NGHTTP2_CB_SETTINGS,
} nghttp2_pending_cb_type;

struct nghttp2_pending_settings_cb {};

struct nghttp2_pending_data_chunks_cb {
  std::shared_ptr<nghttp2_stream_t> handle;
  nghttp2_data_chunk_t* head = nullptr;
  nghttp2_data_chunk_t* tail = nullptr;
  unsigned int nbufs = 0;
  uint8_t flags = NGHTTP2_FLAG_NONE;
};

struct nghttp2_pending_session_send_cb {
  size_t length;
  uv_buf_t* buf;
};

struct nghttp2_pending_headers_cb {
  std::shared_ptr<nghttp2_stream_t> handle;
  nghttp2_headers_category category = NGHTTP2_HCAT_HEADERS;
  nghttp2_header_list* headers = nullptr;
  uint8_t flags = NGHTTP2_FLAG_NONE;
};

struct nghttp2_pending_stream_close_cb {
  std::shared_ptr<nghttp2_stream_t> handle;
  uint32_t error_code = NGHTTP2_NO_ERROR;
};

struct nghttp2_pending_cb_list {
  nghttp2_pending_cb_type type = NGHTTP2_CB_NONE;
  void* cb = nullptr;
  nghttp2_pending_cb_list* next = nullptr;
};

class NodeHTTP2SessionListener {
 public:
  virtual ~NodeHTTP2SessionListener() {}

  virtual void OnStreamInit(std::shared_ptr<nghttp2_stream_t> stream) {}
  virtual void OnStreamFree(nghttp2_stream_t* stream) {}
  virtual void Send(uv_buf_t* buf,
                    size_t length) {}
  virtual void OnHeaders(std::shared_ptr<nghttp2_stream_t> stream,
                         nghttp2_header_list* headers,
                         nghttp2_headers_category cat,
                         uint8_t flags) {}
  virtual void OnStreamClose(int32_t id,
                             uint32_t error_code) {}
  virtual void OnDataChunks(std::shared_ptr<nghttp2_stream_t> stream,
                            std::shared_ptr<nghttp2_data_chunks_t> chunks) {}
  virtual void OnSettings() {}
  virtual ssize_t GetPadding(size_t frameLength,
                             size_t maxFrameLength) { return 0; }
  virtual void OnTrailers(std::shared_ptr<nghttp2_stream_t> stream,
                          MaybeStackBuffer<nghttp2_nv>* nva) {}
  virtual void OnFreeSession() {}
  virtual uv_buf_t* AllocateSend(size_t suggested_size) = 0;

  virtual bool HasGetPaddingCallback() { return false; }
};

// Handle Types
struct nghttp2_session_s {
  uv_loop_t* loop;
  uv_prepare_t prep;
  nghttp2_session* session;
  nghttp2_session_type session_type;
  NodeHTTP2SessionListener* listener;
  nghttp2_pending_cb_list* pending_callbacks_head = nullptr;
  nghttp2_pending_cb_list* pending_callbacks_tail = nullptr;
  nghttp2_pending_cb_list* ready_callbacks_head = nullptr;
  nghttp2_pending_cb_list* ready_callbacks_tail = nullptr;
  std::map<int32_t, std::shared_ptr<nghttp2_stream_t>> streams;
};

struct nghttp2_stream_s {
  nghttp2_session_t* session = nullptr;
  int32_t id = 0;
  int flags = 0;
  nghttp2_stream_write_queue* queue_head = nullptr;
  nghttp2_stream_write_queue* queue_tail = nullptr;
  unsigned int queue_head_index = 0;
  size_t queue_head_offset = 0;
  nghttp2_header_list* current_headers_head = nullptr;
  nghttp2_header_list* current_headers_tail = nullptr;
  nghttp2_headers_category current_headers_category = NGHTTP2_HCAT_HEADERS;
  nghttp2_pending_data_chunks_cb* current_data_chunks_cb = nullptr;
  int reading = -1;
  int32_t prev_local_window_size = 65535;
};

struct nghttp2_stream_write_s {
  void* data;
  int status;
  std::shared_ptr<nghttp2_stream_t> handle;
  nghttp2_stream_write_queue* item;
};

struct nghttp2_data_chunk_s {
  uv_buf_t buf;
  nghttp2_data_chunk_t* next = nullptr;
};

struct nghttp2_data_chunks_s {
  unsigned int nbufs = 0;
  MaybeStackBuffer<uv_buf_t, MAX_BUFFER_COUNT> buf;
};

inline bool nghttp2_session_has_stream(
    nghttp2_session_t* handle,
    int32_t id);

inline bool nghttp2_session_find_stream(
    nghttp2_session_t* handle,
    int32_t id,
    std::shared_ptr<nghttp2_stream_t>* stream_handle);

inline int nghttp2_session_init(
    uv_loop_t*,
    nghttp2_session_t* handle,
    NodeHTTP2SessionListener* listener,
    const nghttp2_session_type type = NGHTTP2_SESSION_SERVER,
    nghttp2_option* options = nullptr,
    nghttp2_mem* mem = nullptr);

inline std::shared_ptr<nghttp2_stream_t> nghttp2_stream_init(
    nghttp2_session_t* handle,
    int32_t id,
    nghttp2_headers_category category);

inline int nghttp2_session_free(nghttp2_session_t* handle);

inline int nghttp2_session_is_alive(
    nghttp2_session_t* handle);

inline ssize_t nghttp2_session_write(
    nghttp2_session_t* handle,
    const uv_buf_t* bufs,
    unsigned int nbufs);

inline int nghttp2_submit_settings(
    nghttp2_session_t* handle,
    const nghttp2_settings_entry iv[],
    size_t niv);

inline int nghttp2_stream_read_start(
    std::shared_ptr<nghttp2_stream_t> handle,
    nghttp2_stream_alloc_cb alloc_cb,
    nghttp2_stream_read_cb read_cb);

inline int nghttp2_stream_read_stop(
    nghttp2_session_t* handle);

inline int nghttp2_stream_write(
    nghttp2_stream_write_t* req,
    std::shared_ptr<nghttp2_stream_t> handle,
    const uv_buf_t bufs[],
    unsigned int nbufs,
    nghttp2_stream_write_cb cb);

inline int nghttp2_submit_response(
    std::shared_ptr<nghttp2_stream_t> handle,
    nghttp2_nv* nva,
    size_t len,
    bool emptyPayload = false);

inline int32_t nghttp2_submit_request(
    nghttp2_session_t* handle,
    nghttp2_priority_spec* prispec,
    nghttp2_nv* nva,
    size_t len,
    std::shared_ptr<nghttp2_stream_t>* assigned,
    bool emptyPayload = true);

inline int nghttp2_submit_info(
    std::shared_ptr<nghttp2_stream_t> handle,
    nghttp2_nv* nva,
    size_t len);

inline int nghttp2_submit_priority(
    std::shared_ptr<nghttp2_stream_t> handle,
    nghttp2_priority_spec* prispec,
    bool silent = false);

inline int nghttp2_submit_rst_stream(
    std::shared_ptr<nghttp2_stream_t> handle,
    const uint32_t code);

inline int nghttp2_submit_push_promise(
    std::shared_ptr<nghttp2_stream_t> handle,
    nghttp2_nv* nva,
    size_t len,
    std::shared_ptr<nghttp2_stream_t>* assigned,
    bool writable = true);

inline int nghttp2_stream_shutdown(
    std::shared_ptr<nghttp2_stream_t> handle);

inline int nghttp2_stream_writable(
    std::shared_ptr<nghttp2_stream_t> handle);

inline void nghttp2_stream_read_start(
    std::shared_ptr<nghttp2_stream_t> handle);

inline void nghttp2_stream_read_stop(
    std::shared_ptr<nghttp2_stream_t> handle);

inline bool nghttp2_stream_is_reading(
    std::shared_ptr<nghttp2_stream_t> handle);

}  // namespace http2
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_HTTP2_CORE_H_
