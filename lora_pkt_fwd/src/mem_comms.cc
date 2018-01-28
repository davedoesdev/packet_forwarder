#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <queue>
#include <vector>
#include <chrono>
#include <mutex>
#include <condition_variable>

using namespace std::chrono_literals;

const int SOCKET_UP = 0,
          SOCKET_DOWN = 1;

template<typename Duration>
class Queue
{
public:
    typedef std::queue<std::vector<uint8_t>> queue_t;

    void reset()
    {
        queue_t empty;
        std::swap(q, empty);
    }

    ssize_t send(const void * /*buf*/, size_t /*len*/,
                 ssize_t hwm, const Duration &timeout)
    {
        if (hwm < 0)
        {
            // hwm < 0 means write always
        }
        else if (timeout < 0)
        {
            // timeout < 0 means block
        }
        else
        {
            // else wait for timeout until buffered data <= hwm
        }

        return 0;
    }

    ssize_t recv(void * /*buf*/, size_t /*len*/,
                 const Duration &timeout)
    {
        if (timeout < 0)
        {
            // timeout < 0 means block
        }
        else
        {
            // else wait for timeout until data is available
        }

        return 0;
    }

private:
    std::mutex m;
    std::condition_variable cv;
    queue_t q;
};

class Link
{
public:
    void reset()
    {
        timeout = -1us;
        from_fwd.reset();
        to_fwd.reset();
    }

    void set_timeout(const struct timeval &timeout)
    {
        if (timerisset(&timeout))
        {
            this->timeout = timeout.tv_sec * 1s + timeout.tv_usec * 1us;
        }
        else
        {
            // setsockopt uses 0 to block
            this->timeout = -1us;
        }
    }

private:
    std::chrono::microseconds timeout = -1us;
    Queue<std::chrono::microseconds> from_fwd, to_fwd;
};

static int next_socket;
static Link links[2];
static int exit_status;
static sighandler_t signal_handler;

extern int lora_pkt_fwd_main();

extern "C" {

int mem_socket(int, int, int)
{
    if (next_socket > SOCKET_DOWN)
    {
        errno = EMFILE;
        return -1;
    }

    links[next_socket].reset();
    return next_socket++;
}

int mem_connect(int sockfd, const struct sockaddr*, socklen_t)
{
    if (sockfd > SOCKET_DOWN)
    {
        errno = EBADF;
        return -1;
    }

    return 0;
}

int mem_setsockopt(int sockfd, int level, int optname,
                   const void* optval, socklen_t optlen)
{
    if (sockfd > SOCKET_DOWN)
    {
        errno = EBADF;
        return -1;
    }

    if (optname != SO_RCVTIMEO)
    {
        errno = ENOPROTOOPT;
        return -1;
    }

    if (!optval)
    {
        errno = EFAULT;
        return -1;
    }

    if ((level != SOL_SOCKET) || (optlen != sizeof(struct timeval)))
    {
        errno = EINVAL;
        return -1;
    }

    links[sockfd].set_timeout(*static_cast<const struct timeval*>(optval));

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
