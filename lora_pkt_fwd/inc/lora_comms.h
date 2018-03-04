/* Read and write LoRa packets. Uses a modified version of the packet forwarder
   which uses in-memory queues instead of UDP. */

#pragma once

#include <stddef.h>
#include <sys/time.h>

const int uplink = 0,   /* Read data packets, write ACK packets. */
          downlink = 1; /* Write data packets, read ACK packets. */

#ifdef __cplusplus
extern "C" {
#endif

/* Start the packet forwarder.
   This won't return until stop() is called on a separate thread.
   Null configuration file directory means current directory. */
int start(const char *cfg_dir);

/* Stop the packet forwarder. */
void stop();

/* Reset the packet forwarder to pre-start state. Call this if you've previously
   started and stopped the packet forwarder and want to start it again.
   Ensure no threads are accessing the packet forwarder when you call this. */
void reset();

/* Read data packets (uplink) or ACK packets (downlink).
   Negative or null timeout blocks.
   Returns number of bytes read or -1 on error and sets errno. */
ssize_t recv_from(int link,
                  void *buf, size_t len,
                  const struct timeval *timeout);

/* Write data packets (downlink) or ACK packets (uplink).
   Positive high-water mark means wait until link has < hwm buffered bytes.
   Negative high-water mark means don't wait (buffer or write straight away).
   Zero high-water mark means write no data.
   Negative or null timeout blocks.
   Returns number of bytes written or -1 on error and sets errno. */
ssize_t send_to(int link,
                const void *buf, size_t len,
                ssize_t hwm, const struct timeval *timeout);

/* You probably won't need these gateway functions but they set the timeout and
   high-water mark when the packet forwarder read and writes to the in-memory
   queues. Note the packet forwarder already sets its read timeout through
   setsockopt(SO_RCVTIMEO), which we intercept in the library. */
void set_gw_send_hwm(int link, const ssize_t hwm);
void set_gw_send_timeout(int link, const struct timeval *timeout);
void set_gw_recv_timeout(int link, const struct timeval *timeout);

/* Recommended buffer sizes for reading and writing packets. */
extern const size_t recv_from_buflen, send_to_buflen;

/* Set a function to call with log messages.
   stream will be stdout or stderr.
   Null logger disables logging (the default).
   Use set_logger(vfprintf) to log to stdio. */
typedef int (*logger_fn)(FILE *stream, const char *format, va_list arg);
void set_logger(logger_fn logger);

#ifdef __cplusplus
/* Function which logs messages to internal queues.
   Use set_logger(log_to_queues) to install it.
   Use get_log_info_message and get_log_error_message to read log messages. */
int log_to_queues(FILE *stream, const char *format, va_list arg);

/* Close the log queues, either immediately or when empty. */
void close_log_queues(bool immediately);

/* Re-open the log queues. */
void reset_log_queues();

/* Read next informational log message from log queue.
   msg receives the message.
   Negative or null timeout blocks.
   Returns number of bytes read or -1 on error and sets errno. */
ssize_t get_log_info_message(std::string &msg, const struct timeval *timeout);

/* Read next eror log message from log queue.
   msg receives the message.
   Negative or null timeout blocks.
   Returns number of bytes read or -1 on error and sets errno. */
ssize_t get_log_error_message(std::string &msg, const struct timeval *timeout);

/* You probably won't need these log functions but they set the timeout,
   high-water mark and maximum log message size if log queues are enabled,
   i.e. you called set_logger(log_to_queues). */
void set_log_max_msg_size(size_t max_size);
void set_log_write_hwm(ssize_t hwm);
void set_log_write_timeout(const struct timeval *timeout);

}
#endif
