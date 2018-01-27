#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdlib.h>

#define SOCKET_UP   0
#define SOCKET_DOWN 1

static int next_socket;
static struct timeval timeout_up, timeout_down;
static int exit_status;
static sighandler_t signal_handler;

extern int lora_pkt_fwd_main();

extern "C" {

int mem_socket(int, int, int)
{
    if (next_socket == SOCKET_UP)
    {
        timeout_up = {};
    }
    else if (next_socket == SOCKET_DOWN)
    {
        timeout_down = {};
    }

    return next_socket++;
}

int mem_connect(int, const struct sockaddr*, socklen_t)
{
    return 0;
}

int mem_setsockopt(int sockfd, int level, int optname,
                   const void* optval, socklen_t optlen)
{
    if ((level == SOL_SOCKET) && (optname == SO_RCVTIMEO) &&
        optval && (optlen == sizeof(struct timeval)))
    {
        auto *ptimeout = static_cast<const struct timeval*>(optval);
        if (sockfd == SOCKET_UP)
        {
            timeout_up = *ptimeout;
        }
        else if (sockfd == SOCKET_DOWN)
        {
            timeout_down = *ptimeout;
        }
    }

    return 0;
}

ssize_t mem_send(int /*sockfd*/, const void * /*buf*/, size_t /*len*/, int /*flags*/)
{
    return 0;
}

ssize_t mem_recv(int /*sockfd*/, void * /*buf*/, size_t /*len*/, int /*flags*/)
{
    return 0;
}

int mem_shutdown(int sockfd, int)
{
    if (sockfd == SOCKET_DOWN)
    {
        next_socket = 0;
    }

    return 0;
}

void mem_exit(int status)
{
    exit_status = status;
}

void mem_sigaction(int signum,
                   const struct sigaction *act,
                   struct sigaction*)
{
    if ((signum == SIGTERM) && act)
    {
        signal_handler = act->sa_handler;
    }
}

int start()
{
    next_socket = 0;
    lora_pkt_fwd_main();
    return exit_status;
}

void stop()
{
    signal_handler(SIGTERM);
}

// we'll have two sockets, up first then down
// each should have a read and write queue
// then export functions to read and write from each
/*
read_from_uplink_queue
write_to_uplink_queue

write_to_downlink_queue
read_from_downlink_queue
*/

}
