/* (c)2018 David Halls, MIT licence */

#include <lora_comms.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

#define PROTOCOL_VERSION 2

#define PKT_PUSH_DATA    0
#define PKT_PUSH_ACK     1
#define PKT_PULL_DATA    2
#define PKT_PULL_RESP    3
#define PKT_PULL_ACK     4

void *thread_ack(void *arg) {
    int link = (int)(intptr_t)arg;
    uint8_t databuf[recv_from_buflen];

    while (1) {
        /* wait to receive a packet */
        ssize_t n = recv_from(link, databuf, sizeof databuf, NULL);
        if (n == -1) {
            return NULL;
        }

        if (n < 12) { /* not enough bytes for packet from gateway */
            continue;
        }

        if (databuf[0] != PROTOCOL_VERSION) { /* check protocol version number */
            continue;
        }

        if ((link == uplink) && (databuf[3] == PKT_PUSH_DATA)) {
            databuf[3] = PKT_PUSH_ACK;
        } else if ((link == downlink) && (databuf[3] == PKT_PULL_DATA)) {
            databuf[3] = PKT_PULL_ACK;
        } else {
            continue;
        }

        /* send acknowledgement to forwarder */
        n = send_to(link, (void*)databuf, 4, -1, NULL);
        if (n == -1) {
            return NULL;
        }
    }
}

void sig_handler(int signum __attribute__((unused))) {
    stop();
}

int main(int argc, char **argv) {
    set_logger(vfprintf);

    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigaction(SIGINT, &sigact, NULL); /* Ctrl-C */

    pthread_t thrid_uplink, thrid_downlink;

    if (pthread_create(&thrid_uplink, NULL, thread_ack, (void*)(intptr_t)uplink) != 0) {
        return EXIT_FAILURE;
    }

    if (pthread_create(&thrid_downlink, NULL, thread_ack, (void*)(intptr_t)downlink) != 0) {
        return EXIT_FAILURE;
    }

    int r = start(argc > 1 ? argv[1] : NULL);

    pthread_join(thrid_uplink, NULL);
    pthread_join(thrid_downlink, NULL);

    return r;
}
