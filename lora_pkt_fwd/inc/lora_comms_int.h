#pragma once

#include "lora_comms.h"

#ifdef __cplusplus
extern "C" {
#endif

/* You probably won't need these gateway functions but they set the timeout and
   high-water mark when the packet forwarder read and writes to the in-memory
   queues. Note the packet forwarder already sets its read timeout through
   setsockopt(SO_RCVTIMEO), which we intercept in the library. */
void set_gw_send_hwm(enum comm_link link, const ssize_t hwm);
void set_gw_send_timeout(enum comm_link link, const struct timeval *timeout);
void set_gw_recv_timeout(enum comm_link link, const struct timeval *timeout);

/* You probably won't need these log functions but they set the timeout,
   high-water mark and maximum log message size if log queues are enabled,
   i.e. you called set_logger(log_to_queues). */
void set_log_write_hwm(ssize_t hwm);
void set_log_write_timeout(const struct timeval *timeout);
void set_log_max_msg_size(size_t max_size);

/* Type of get_log_*_message */
typedef ssize_t (*get_log_message_fn)(char *msg, size_t len,
                                      const struct timeval *timeout);

#ifdef __cplusplus
}
#endif
