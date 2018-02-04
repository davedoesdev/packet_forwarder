/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
    Ask a gateway to emit packets using GW <-> server protocol

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
#include <stdbool.h>    /* bool type */
#include <stdio.h>      /* printf fprintf sprintf fopen fputs */
#include <unistd.h>     /* getopt access usleep */

#include <string.h>     /* memset */
#include <signal.h>     /* sigaction */
#include <stdlib.h>     /* exit codes */
#include <errno.h>      /* error messages */

#include <arpa/inet.h>  /* ntohl */
#include <pthread.h>    /* pthread_create, pthread_join */

#include <base64.h>     /* bin_to_b64 */

#include <lora_comms.h>

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#define MSG(args...)    fprintf(stderr, args) /* message that is destined to the user */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#define PROTOCOL_VERSION 2

#define PKT_PUSH_DATA   0
#define PKT_PUSH_ACK    1
#define PKT_PULL_DATA   2
#define PKT_PULL_RESP   3
#define PKT_PULL_ACK    4
#define PKT_TX_ACK      5

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DECLARATION ---------------------------------------- */

static void sig_handler(int sigio);

void usage (void);

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DEFINITION ----------------------------------------- */

#define UNUSED(x) (void)(x)

static void sig_handler(int signum)
{
    UNUSED(signum);
    stop();
}

static void *thread_fwd(void *arg)
{
    return (void*)(intptr_t)start((char*)arg);
}

static int wait_for_fwd(pthread_t thrid_fwd)
{
    void *r = (void*)(intptr_t)EXIT_FAILURE;
    pthread_join(thrid_fwd, &r);
    return (int)(intptr_t)r;
}

/* describe command line options */
void usage(void) {
    MSG("Usage: util_tx_test {options}\n");
    MSG("Available options:\n");
    MSG(" -h print this help\n");
    MSG(" -c <str> configuration directory\n");
    MSG(" -f <float> target frequency in MHz\n");
    MSG(" -m <str> Modulation type ['LORA, 'FSK']\n");
    MSG(" -s <int> Spreading Factor [7:12]\n");
    MSG(" -b <int> Modulation bandwidth in kHz [125,250,500]\n");
    MSG(" -d <uint> FSK frequency deviation in kHz [1:250]\n");
    MSG(" -r <float> FSK bitrate in kbps [0.5:250]\n");
    MSG(" -p <int> RF power (dBm)\n");
    MSG(" -z <uint> Payload size in bytes [9:255]\n");
    MSG(" -t <int> pause between packets (ms)\n");
    MSG(" -x <int> numbers of times the sequence is repeated\n");
    MSG(" -v <uint> test ID, inserted in payload for PER test [0:255]\n");
    MSG(" -i send packet using inverted modulation polarity \n");
}

/* -------------------------------------------------------------------------- */
/* --- MAIN FUNCTION -------------------------------------------------------- */

int main(int argc, char **argv)
{
    int i, j, x;
    unsigned int xu;
    char arg_s[64];

    /* application parameters */
    char mod[64] = "LORA"; /* LoRa modulation by default */
    float f_target = 866.0; /* target frequency */
    int sf = 10; /* SF10 by default */
    int bw = 125; /* 125kHz bandwidth by default */
    int pow = 14; /* 14 dBm by default */
    int delay = 1000; /* 1 second between packets by default */
    int repeat = 1; /* sweep only once by default */
    bool invert = false;
    float br_kbps = 50; /* 50 kbps by default */
    uint8_t fdev_khz = 25; /* 25 khz by default */

    /* packet payload variables */
    int payload_size = 9; /* minimum size for PER frame */
    uint8_t payload_bin[255];
    char payload_b64[341];
    int payload_index;

    /* PER payload variables */
    uint8_t id = 0;

    /* variables for receiving and sending packets */
    uint8_t databuf[send_to_buflen];
    uint8_t databuf2[recv_from_buflen];
    int buff_index;
    int byte_nb;

    /* variables for gateway identification */
    uint32_t raw_mac_h; /* Most Significant Nibble, network order */
    uint32_t raw_mac_l; /* Least Significant Nibble, network order */
    uint64_t gw_mac; /* MAC address of the client (gateway) */

    /* variable for configuration directory */
    char cfg_dir[1024] = {0};

    /* parse command line options */
    while ((i = getopt (argc, argv, "hc:f:m:s:b:d:r:p:z:t:x:v:i")) != -1) {
        switch (i) {
            case 'h':
                usage();
                return EXIT_FAILURE;
                break;

            case 'c': /* -c <str> configuration directory */
                strncpy(cfg_dir, optarg, sizeof cfg_dir);
                break;

            case 'f': /* -f <float> target frequency in MHz */
                i = sscanf(optarg, "%f", &f_target);
                if ((i != 1) || (f_target < 30.0) || (f_target > 3000.0)) {
                    MSG("ERROR: invalid TX frequency\n");
                    return EXIT_FAILURE;
                }
                break;

            case 'm': /* -m <str> Modulation type */
                i = sscanf(optarg, "%s", arg_s);
                if ((i != 1) || ((strcmp(arg_s, "LORA") != 0) && (strcmp(arg_s, "FSK")))) {
                    MSG("ERROR: invalid modulation type\n");
                    usage();
                    return EXIT_FAILURE;
                } else {
                    sprintf(mod, "%s", arg_s);
                }
                break;

            case 's': /* -s <int> Spreading Factor */
                i = sscanf(optarg, "%i", &sf);
                if ((i != 1) || (sf < 7) || (sf > 12)) {
                    MSG("ERROR: invalid spreading factor\n");
                    return EXIT_FAILURE;
                }
                break;

            case 'b': /* -b <int> Modulation bandwidth in kHz */
                i = sscanf(optarg, "%i", &bw);
                if ((i != 1) || ((bw != 125) && (bw != 250) && (bw != 500))) {
                    MSG("ERROR: invalid LORA bandwidth\n");
                    return EXIT_FAILURE;
                }
                break;

            case 'd': /* -d <uint> FSK frequency deviation */
                i = sscanf(optarg, "%u", &xu);
                if ((i != 1) || (xu < 1) || (xu > 250)) {
                    MSG("ERROR: invalid FSK frequency deviation\n");
                    usage();
                    return EXIT_FAILURE;
                } else {
                    fdev_khz = (uint8_t)xu;
                }
                break;

            case 'r': /* -q <float> FSK bitrate */
                i = sscanf(optarg, "%f", &br_kbps);
                if ((i != 1) || (br_kbps < 0.5) || (br_kbps > 250)) {
                    MSG("ERROR: invalid FSK bitrate\n");
                    usage();
                    return EXIT_FAILURE;
                }
                break;

            case 'p': /* -p <int> RF power */
                i = sscanf(optarg, "%i", &pow);
                if ((i != 1) || (pow < 0) || (pow > 30)) {
                    MSG("ERROR: invalid RF power\n");
                    return EXIT_FAILURE;
                }
                break;

            case 'z': /* -z <uint> Payload size */
                i = sscanf(optarg, "%i", &payload_size);
                if ((i != 1) || (payload_size < 9) || (payload_size > 255)) {
                    MSG("ERROR: invalid payload size\n");
                    usage();
                    return EXIT_FAILURE;
                }
                break;

            case 't': /* -t <int> pause between RF packets (ms) */
                i = sscanf(optarg, "%i", &delay);
                if ((i != 1) || (delay < 0)) {
                    MSG("ERROR: invalid time between RF packets\n");
                    return EXIT_FAILURE;
                }
                break;

            case 'x': /* -x <int> numbers of times the sequence is repeated */
                i = sscanf(optarg, "%u", &repeat);
                if ((i != 1) || (repeat < 1)) {
                    MSG("ERROR: invalid number of repeats\n");
                    return EXIT_FAILURE;
                }
                break;

            case 'v': /* -v <uint> test Id */
                i = sscanf(optarg, "%u", &xu);
                if ((i != 1) || ((xu < 1) && (xu > 255))) {
                    MSG("ERROR: invalid Id\n");
                    return EXIT_FAILURE;
                } else {
                    id = (uint8_t)xu;
                }
                break;

            case 'i': /* -i send packet using inverted modulation polarity */
                invert = true;
                break;

            default:
                MSG("ERROR: argument parsing failure, use -h option for help\n");
                usage();
                return EXIT_FAILURE;
        }
    }

    /* configure signal handling */
    struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigaction(SIGQUIT, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);

    /* display setup summary */
    if (strcmp(mod, "FSK") == 0) {
        MSG("INFO: %i FSK pkts @%f MHz (FDev %u kHz, Bitrate %.2f kbps, %uB payload) %i dBm, %i ms between each\n", repeat, f_target, fdev_khz, br_kbps, payload_size, pow, delay);
    } else {
        MSG("INFO: %i LoRa pkts @%f MHz (BW %u kHz, SF%i, %uB payload) %i dBm, %i ms between each\n", repeat, f_target, bw, sf, payload_size, pow, delay);
    }

    /* start forwarder thread */
    pthread_t thrid_fwd;
    if (pthread_create(&thrid_fwd, NULL, thread_fwd, (void*)(cfg_dir[0] ? cfg_dir : NULL)) != 0)
    {
        MSG("ERROR: failed to create forwarder thread\n");
        return EXIT_FAILURE;
    }

    /* wait to receive a PULL_DATA request packet */
    MSG("INFO: waiting to receive a PULL_DATA request\n");
    while (1) {
        byte_nb = recv_from(downlink, databuf, sizeof databuf, NULL);
        if (byte_nb < 0) {
            if (errno == EBADF) {
                return wait_for_fwd(thrid_fwd);
            }
            MSG("WARNING: recv_from returned an error %s\n", strerror(errno));
        } else if ((byte_nb < 12) || (databuf[0] != PROTOCOL_VERSION) || (databuf[3] != PKT_PULL_DATA)) {
            MSG("INFO: packet received, not PULL_DATA request\n");
        } else {
            break; /* success! */
        }
    }

    /* retrieve gateway MAC from the request */
    raw_mac_h = *((uint32_t *)(databuf+4));
    raw_mac_l = *((uint32_t *)(databuf+8));
    gw_mac = ((uint64_t)ntohl(raw_mac_h) << 32) + (uint64_t)ntohl(raw_mac_l);

    /* display info about the sender */
    MSG("INFO: PULL_DATA request received from gateway 0x%08X%08X\n", (uint32_t)(gw_mac >> 32), (uint32_t)(gw_mac & 0xFFFFFFFF));

    /* Send PULL_ACK */
    databuf[3] = PKT_PULL_ACK;
    byte_nb = send_to(downlink, (void*)databuf, 4, -1, NULL);
    if (byte_nb == -1) {
        if (errno == EBADF) {
            return wait_for_fwd(thrid_fwd);
        }
        MSG("WARNING: send_to returned an error %s\n", strerror(errno));
    }

    /* PKT_PULL_RESP datagrams header */
    databuf[0] = PROTOCOL_VERSION;
    databuf[1] = 0; /* no token */
    databuf[2] = 0; /* no token */
    databuf[3] = PKT_PULL_RESP;
    buff_index = 4;

    /* start of JSON structure */
    memcpy((void *)(databuf + buff_index), (void *)"{\"txpk\":{\"imme\":true", 20);
    buff_index += 20;

    /* TX frequency */
    i = snprintf((char *)(databuf + buff_index), 20, ",\"freq\":%.6f", f_target);
    if ((i>=0) && (i < 20)) {
        buff_index += i;
    } else {
        MSG("ERROR: snprintf failed line %u\n", (__LINE__ - 4));
        return wait_for_fwd(thrid_fwd);
    }

    /* RF channel */
    memcpy((void *)(databuf + buff_index), (void *)",\"rfch\":0", 9);
    buff_index += 9;

    /* TX power */
    i = snprintf((char *)(databuf + buff_index), 12, ",\"powe\":%i", pow);
    if ((i>=0) && (i < 12)) {
        buff_index += i;
    } else {
        MSG("ERROR: snprintf failed line %u\n", (__LINE__ - 4));
        return wait_for_fwd(thrid_fwd);
    }

    /* modulation type and parameters */
    if (strcmp(mod, "FSK") == 0) {
        i = snprintf((char *)(databuf + buff_index), 50, ",\"modu\":\"FSK\",\"datr\":%u,\"fdev\":%u", (unsigned int)(br_kbps*1e3), (unsigned int)(fdev_khz*1e3));
        if ((i>=0) && (i < 50)) {
            buff_index += i;
        } else {
            MSG("ERROR: snprintf failed line %u\n", (__LINE__ - 4));
            return wait_for_fwd(thrid_fwd);
        }
    } else {
        i = snprintf((char *)(databuf + buff_index), 50, ",\"modu\":\"LORA\",\"datr\":\"SF%iBW%i\",\"codr\":\"4/6\"", sf, bw);
        if ((i>=0) && (i < 50)) {
            buff_index += i;
        } else {
            MSG("ERROR: snprintf failed line %u\n", (__LINE__ - 4));
            return wait_for_fwd(thrid_fwd);
        }
    }

    /* signal polarity */
    if (invert) {
        memcpy((void *)(databuf + buff_index), (void *)",\"ipol\":true", 12);
        buff_index += 12;
    } else {
        memcpy((void *)(databuf + buff_index), (void *)",\"ipol\":false", 13);
        buff_index += 13;
    }

    /* Preamble size */
    if (strcmp(mod, "LORA") == 0) {
        memcpy((void *)(databuf + buff_index), (void *)",\"prea\":8", 9);
        buff_index += 9;
    }

    /* payload size */
    i = snprintf((char *)(databuf + buff_index), 12, ",\"size\":%i", payload_size);
    if ((i>=0) && (i < 12)) {
        buff_index += i;
    } else {
        MSG("ERROR: snprintf failed line %u\n", (__LINE__ - 4));
        return wait_for_fwd(thrid_fwd);
    }

    /* payload JSON object */
    memcpy((void *)(databuf + buff_index), (void *)",\"data\":\"", 9);
    buff_index += 9;
    payload_index = buff_index; /* keep the value where the payload content start */

    /* payload place-holder & end of JSON structure */
    x = bin_to_b64(payload_bin, payload_size, payload_b64, sizeof payload_b64); /* dummy conversion to get exact size */
    if (x >= 0) {
        buff_index += x;
    } else {
        MSG("ERROR: bin_to_b64 failed line %u\n", (__LINE__ - 4));
        return wait_for_fwd(thrid_fwd);
    }

    /* Close JSON structure */
    memcpy((void *)(databuf + buff_index), (void *)"\"}}", 3);
    buff_index += 3; /* ends up being the total length of payload */

    /* main loop */
    for (i = 0; i < repeat; ++i) {
        /* fill payload */
        payload_bin[0] = id;
        payload_bin[1] = (uint8_t)(i >> 24);
        payload_bin[2] = (uint8_t)(i >> 16);
        payload_bin[3] = (uint8_t)(i >> 8);
        payload_bin[4] = (uint8_t)(i);
        payload_bin[5] = 'P';
        payload_bin[6] = 'E';
        payload_bin[7] = 'R';
        payload_bin[8] = (uint8_t)(payload_bin[0] + payload_bin[1] + payload_bin[2] + payload_bin[3] + payload_bin[4] + payload_bin[5] + payload_bin[6] + payload_bin[7]);
        for (j = 0; j < (payload_size - 9); j++) {
            payload_bin[9+j] = j;
        }

#if 0
        for (j = 0; j < payload_size; j++ ) {
            printf("0x%02X ", payload_bin[j]);
        }
        printf("\n");
#endif

        /* encode the payload in Base64 */
        x = bin_to_b64(payload_bin, payload_size, payload_b64, sizeof payload_b64);
        if (x >= 0) {
            memcpy((void *)(databuf + payload_index), (void *)payload_b64, x);
        } else {
            MSG("ERROR: bin_to_b64 failed line %u\n", (__LINE__ - 4));
            return wait_for_fwd(thrid_fwd);
        }

        databuf[1] = (uint8_t)rand(); /* random token */
        databuf[2] = (uint8_t)rand(); /* random token */

        /* send packet to the gateway */
        byte_nb = send_to(downlink, (void *)databuf, buff_index, -1, NULL);
        if (byte_nb == -1) {
            if (errno == EBADF) {
                return wait_for_fwd(thrid_fwd);
            }
            MSG("WARNING: send_to returned an error %s\n", strerror(errno));
        } else {
            MSG("INFO: packet #%i sent successfully\n", i);
        }

        /* wait to receive a TX_ACK request packet */
        MSG("INFO: waiting to receive a TX_ACK request\n");
        while (1) {
            byte_nb = recv_from(downlink, databuf2, sizeof databuf2, NULL);
            if (byte_nb < 0) {
                if (errno == EBADF) {
                    return wait_for_fwd(thrid_fwd);
                }
                MSG("WARNING: recv_from returned an error %s\n", strerror(errno));
            } else if ((byte_nb < 12) || (databuf2[0] != PROTOCOL_VERSION) || (databuf2[3] != PKT_TX_ACK)) {
                MSG("INFO: packet received, not TX_ACK request\n");
            } else if ((databuf2[1] != databuf[1]) ||
                       (databuf2[2] != databuf[2])) {
                MSG("INFO: TX_ACK received but token doesn't match\n");
            } else {
                break; /* success! */
            }
        }

        /* wait inter-packet delay */
        usleep(delay * 1000);
    }
}

/* --- EOF ------------------------------------------------------------------ */
