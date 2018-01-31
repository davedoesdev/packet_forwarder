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

#include <pthread.h>    /* pthread_create, pthread_join */

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

static void sig_handler(int signum)
{
    UNUSED(signum);
    stop();
}

static void *thread_sink(void *arg)
{
    int link = (int)(intptr_t)arg;

    /* variables for receiving packets */
    uint8_t databuf[4096];
    int byte_nb;

    while (1)
    {
        byte_nb = recv_from(link, databuf, sizeof databuf, NULL);
        if (byte_nb == -1)
        {
            MSG("ERROR: link %d recv_from returned %s\n", link, strerror(errno));
            return NULL;
        }
        printf("Link %d got packet %d bytes long\n", link, byte_nb);
    }
}

int main(int argc, char **argv)
{
    /* configure signal handling */
    struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigaction(SIGQUIT, &sigact, NULL); /* Ctrl-\ */
    sigaction(SIGINT, &sigact, NULL); /* Ctrl-C */
    sigaction(SIGTERM, &sigact, NULL); /* default "kill" command */

    pthread_t thrid_uplink, thrid_downlink;

    if (pthread_create(&thrid_uplink, NULL, thread_sink, (void*)(intptr_t)uplink) != 0)
    {
        MSG("ERROR: failed to create uplink thread\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&thrid_downlink, NULL, thread_sink, (void*)(intptr_t)downlink) != 0)
    {
        MSG("ERROR: failed to create downlink thread\n");
        return EXIT_FAILURE;
    }

    MSG("INFO: util_sink listening\n");
    int r = start(argc > 1 ? argv[1] : NULL);

    pthread_join(thrid_uplink, NULL);
    pthread_join(thrid_downlink, NULL);

    return r;
}
