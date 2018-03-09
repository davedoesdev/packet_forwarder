/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
    Network sink, receives UDP packets and sends an acknowledge

License: Revised BSD License, see LICENSE.TXT file include in the project
Maintainer: Sylvain Miermont
*/
/*
Changes to work with packet_forwarder_shared (c)2018 David Halls, MIT licence
*/


/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

/* fix an issue between POSIX and C99 */
#if __STDC_VERSION__ >= 199901L
    #define _XOPEN_SOURCE 600
#else
    #define _XOPEN_SOURCE 500
#endif

#include <stdint.h>     /* C99 types */
#include <stdio.h>      /* printf, fprintf, sprintf, fopen, fputs */
#include <unistd.h>     /* usleep */

#include <string.h>     /* memset */
#include <time.h>       /* time, clock_gettime, strftime, gmtime, clock_nanosleep*/
#include <stdlib.h>     /* atoi, exit */
#include <errno.h>      /* error messages */
#include <signal.h>     /* sigaction */

#include <arpa/inet.h>  /* ntohl */
#include <pthread.h>    /* pthread_create, pthread_join */

#include <lora_comms.h>

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#define STRINGIFY(x)    #x
#define STR(x)          STRINGIFY(x)
#define MSG(args...)    fprintf(stderr, args) /* message that is destined to the user */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#define PROTOCOL_VERSION 2

#define PKT_PUSH_DATA    0
#define PKT_PUSH_ACK     1
#define PKT_PULL_DATA    2
#define PKT_PULL_RESP    3
#define PKT_PULL_ACK     4

/* -------------------------------------------------------------------------- */
/* --- MAIN FUNCTION -------------------------------------------------------- */

#define UNUSED(x) (void)(x)

static void sig_handler(int signum)
{
    UNUSED(signum);
    stop();
}

static void *thread_ack(void *arg)
{
    int link = (int)(intptr_t)arg;

    /* variables for receiving and sending packets */
    uint8_t databuf[recv_from_buflen];
    int byte_nb;

    /* variables for protocol management */
    uint32_t raw_mac_h; /* Most Significant Nibble, network order */
    uint32_t raw_mac_l; /* Least Significant Nibble, network order */
    uint64_t gw_mac; /* MAC address of the client (gateway) */
    uint8_t ack_command;

    while (1)
    {
        /* wait to receive a packet */
        byte_nb = recv_from(link, databuf, sizeof databuf, NULL);
        if (byte_nb == -1)
        {
            MSG("ERROR: link %d recv_from returned %s\n", link, strerror(errno));
            return NULL;
        }
        printf(" -> pkt in, link=%d, %d bytes", link, byte_nb);

        /* check and parse the payload */
        if (byte_nb < 12) { /* not enough bytes for packet from gateway */
            printf(" (too short for GW <-> MAC protocol)\n");
            continue;
        }

        /* don't touch the token in position 1-2, it will be sent back "as is" for acknowledgement */
        if (databuf[0] != PROTOCOL_VERSION) { /* check protocol version number */
            printf(", invalid version %u\n", databuf[0]);
            continue;
        }

        raw_mac_h = *((uint32_t *)(databuf+4));
        raw_mac_l = *((uint32_t *)(databuf+8));
        gw_mac = ((uint64_t)ntohl(raw_mac_h) << 32) + (uint64_t)ntohl(raw_mac_l);

        /* interpret gateway command */
        if ((link == uplink) && (databuf[3] == PKT_PUSH_DATA))
        {
            printf(", PUSH_DATA from gateway 0x%08X%08X\n", (uint32_t)(gw_mac >> 32), (uint32_t)(gw_mac & 0xFFFFFFFF));
            ack_command = PKT_PUSH_ACK;
            printf("<-  pkt out, PUSH_ACK");
        }
        else if ((link == downlink) && (databuf[3] == PKT_PULL_DATA))
        {
            printf(", PULL_DATA from gateway 0x%08X%08X\n", (uint32_t)(gw_mac >> 32), (uint32_t)(gw_mac & 0xFFFFFFFF));
            ack_command = PKT_PULL_ACK;
            printf("<-  pkt out, PULL_ACK");
        }
        else
        {
            printf(", unexpected command %u\n", databuf[3]);
            continue;
        }

        /* add some artificial latency */
        usleep(30000); /* 30 ms */

        /* send acknowledge and check return value */
        databuf[3] = ack_command;
        byte_nb = send_to(link, (void*)databuf, 4, -1, NULL);
        if (byte_nb == -1) {
            printf(", send error: %s\n", strerror(errno));
            return NULL;
        }
        printf(", %d bytes sent\n", byte_nb);
    }
}

int main(int argc, char **argv)
{
    set_logger(vfprintf);

    /* configure signal handling */
    struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigaction(SIGQUIT, &sigact, NULL); /* Ctrl-\ */
    sigaction(SIGINT, &sigact, NULL); /* Ctrl-C */
    sigaction(SIGTERM, &sigact, NULL); /* default "kill" command */

    pthread_t thrid_uplink, thrid_downlink;

    if (pthread_create(&thrid_uplink, NULL, thread_ack, (void*)(intptr_t)uplink) != 0)
    {
        MSG("ERROR: failed to create uplink thread\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&thrid_downlink, NULL, thread_ack, (void*)(intptr_t)downlink) != 0)
    {
        MSG("ERROR: failed to create downlink thread\n");
        return EXIT_FAILURE;
    }


    MSG("INFO: util_ack listening\n");
    int r = start(argc > 1 ? argv[1] : NULL);

    pthread_join(thrid_uplink, NULL);
    pthread_join(thrid_downlink, NULL);

    return r;
}
