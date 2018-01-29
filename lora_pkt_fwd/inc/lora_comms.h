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
   This won't return until stop() is called on a separate thread. */
int start();

/* Stop the packet forwarder. */
void stop();

/* Read data packets (uplink) or ACK packets (downlink).
   Negative or null timeout blocks. */
ssize_t recv_from(int link,
                  void * buf, size_t len,
                  const struct timeval *timeout);

/* Write data packets (downlink) or ACK packets (uplink).
   Non-negative high-water mark means wait until link has <= hwm buffered bytes.
   Negative or null timeout blocks. */
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

#ifdef __cplusplus
}
#endif