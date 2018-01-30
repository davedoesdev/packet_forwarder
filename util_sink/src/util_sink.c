/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
    Network sink, receives UDP packets on certain ports and discards them

License: Revised BSD License, see LICENSE.TXT file include in the project
Maintainer: Sylvain Miermont
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

#include <string.h>     /* memset */
#include <time.h>       /* time, clock_gettime, strftime, gmtime, clock_nanosleep*/
#include <stdlib.h>     /* atoi, exit */
#include <errno.h>      /* error messages */
#include <signal.h>     /* sigaction */

#include <pthread.h>

#include <lora_comms.h>

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#define STRINGIFY(x)    #x
#define STR(x)          STRINGIFY(x)
#define MSG(args...)    fprintf(stderr, args) /* message that is destined to the user */

/* -------------------------------------------------------------------------- */
/* --- MAIN FUNCTION -------------------------------------------------------- */

#define UNUSED(x) (void)(x)

static void sig_handler(int sigio)
{
    UNUSED(sigio);
    stop();
}

int main(int argc, char **argv)
{
    struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */

    /* configure signal handling */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigaction(SIGQUIT, &sigact, NULL); /* Ctrl-\ */
    sigaction(SIGINT, &sigact, NULL); /* Ctrl-C */
    sigaction(SIGTERM, &sigact, NULL); /* default "kill" command */

    /* variables for receiving packets */
    uint8_t databuf[4096];
    int byte_nb;

    // start threads listening on uplink and downlink
    // test ^C

    MSG("INFO: util_sink listening\n");
    return start(argc > 1 ? argv[1] : NULL);
/*
    while (1) {
        byte_nb = recvfrom(sock, databuf, sizeof databuf, 0, (struct sockaddr *)&dist_addr, &addr_len);
        if (byte_nb == -1) {
            MSG("ERROR: recvfrom returned %s \n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        getnameinfo((struct sockaddr *)&dist_addr, addr_len, host_name, sizeof host_name, port_name, sizeof port_name, NI_NUMERICHOST);
        printf("Got packet from host %s port %s, %i bytes long\n", host_name, port_name, byte_nb);
    }
*/
}
