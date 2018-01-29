#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <queue>
#include <vector>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <exception>

#include "lora_comms.h"

using namespace std::chrono_literals;

template<typename Duration>
class Queue
{
public:
    typedef std::vector<uint8_t> element_t;
    typedef std::queue<element_t> queue_t;

    void reset()
    {
        queue_t empty;
        std::swap(q, empty);
        size = 0;
    }

    ssize_t send(const void *buf, size_t len,
                 ssize_t hwm, const Duration &timeout)
    {
        std::unique_lock<std::mutex> lock(m);

        if ((hwm >= 0) && (size > hwm))
        {
            // wait for until buffered data size <= hwm
            auto pred = [this, hwm] { return this->size <= hwm; };

            if (timeout < Duration::zero())
            {
                // timeout < 0 means block
                cv.wait(lock, pred);
            }
            else if ((timeout == Duration::zero()) ||
                     !cv.wait_for(lock, timeout, pred))
            {
                errno = EAGAIN;
                return -1;
            }
        }

        auto bytes = static_cast<const uint8_t*>(buf);
        q.emplace(bytes, &bytes[len]);
        size += len;

        return 0;
    }

    ssize_t recv(void *buf, size_t len, const Duration &timeout)
    {
        std::unique_lock<std::mutex> lock(m);

        if (q.empty())
        {
            auto pred = [this] { return !this->q.empty(); };

            if (timeout < Duration::zero())
            {
                // timeout < 0 means block
                cv.wait(lock, pred);
            }
            else if ((timeout == Duration::zero()) ||
                     !cv.wait_for(lock, timeout, pred))
            {
                errno = EAGAIN;
                return -1;
            }
        }

        element_t &el = q.front();
        ssize_t r = std::min(el.size(), len);
        memcpy(buf, el.data(), r);
        q.pop();
        size -= el.size();

        return r;
    }

private:
    std::mutex m;
    std::condition_variable cv;
    queue_t q;
    ssize_t size = 0;
};

class Link
{
public:
    void reset()
    {
        from_fwd_send_hwm = -1;
        from_fwd_send_timeout = -1us;
        to_fwd_recv_timeout = -1us;
        from_fwd.reset();
        to_fwd.reset();
    }

    void set_from_fwd_send_hwm(const ssize_t hwm)
    {
        from_fwd_send_hwm = hwm;
    }

    void set_from_fwd_send_timeout(const std::chrono::microseconds &timeout)
    {
        from_fwd_send_timeout = timeout;
    }

    void set_to_fwd_recv_timeout(const std::chrono::microseconds &timeout)
    {
        to_fwd_recv_timeout = timeout;
    }
    
    ssize_t from_fwd_send(const void *buf, size_t len)
    {
        return from_fwd.send(buf, len,
                             from_fwd_send_hwm, from_fwd_send_timeout);
    }

    ssize_t from_fwd_recv(void *buf, size_t len,
                          const std::chrono::microseconds &timeout)
    {
        return from_fwd.recv(buf, len, timeout);
    }

    ssize_t to_fwd_send(const void *buf, size_t len,
                        ssize_t hwm, const std::chrono::microseconds &timeout)
    {
        return to_fwd.send(buf, len, hwm, timeout);
    }

    ssize_t to_fwd_recv(void *buf, size_t len)
    {
        return to_fwd.recv(buf, len, to_fwd_recv_timeout);
    }

private:
    ssize_t from_fwd_send_hwm = -1;
    std::chrono::microseconds from_fwd_send_timeout = -1us;
    std::chrono::microseconds to_fwd_recv_timeout = -1us;
    Queue<std::chrono::microseconds> from_fwd, to_fwd;
};

static int next_socket;
static Link links[2];
static sighandler_t signal_handler;

struct ExitException : public std::exception
{
    int status;
};

std::chrono::microseconds to_microseconds(const struct timeval *tv)
{
    return tv ? (tv->tv_sec * 1s + tv->tv_usec * 1us) : -1us;
}

extern "C" {

extern int lora_pkt_fwd_main();

int mem_socket(int, int, int)
{
    if (next_socket > downlink)
    {
        errno = EMFILE;
        return -1;
    }

    links[next_socket].reset();
    return next_socket++;
}

int mem_connect(int sockfd, const struct sockaddr*, socklen_t)
{
    if ((sockfd < uplink) || (sockfd > downlink))
    {
        errno = EBADF;
        return -1;
    }

    return 0;
}

int mem_setsockopt(int sockfd, int level, int optname,
                   const void* optval, socklen_t optlen)
{
    if ((sockfd < uplink) || (sockfd > downlink))
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

    auto ptimeout = static_cast<const struct timeval*>(optval);

    links[sockfd].set_to_fwd_recv_timeout(
        // setsockopt uses 0 to block
        to_microseconds(timerisset(ptimeout) ? ptimeout : nullptr));

    return 0;
}

ssize_t mem_send(int sockfd, const void *buf, size_t len, int /*flags*/)
{
    if ((sockfd < uplink) || (sockfd > downlink))
    {
        errno = EBADF;
        return -1;
    }

    return links[sockfd].from_fwd_send(buf, len);
}

ssize_t mem_recv(int sockfd, void *buf, size_t len, int /*flags*/)
{
    if ((sockfd < uplink) || (sockfd > downlink))
    {
        errno = EBADF;
        return -1;
    }

    return links[sockfd].to_fwd_recv(buf, len);
}

int mem_shutdown(int sockfd, int)
{
    if ((sockfd < uplink) || (sockfd > downlink))
    {
        errno = EBADF;
        return -1;
    }

    if (sockfd == downlink)
    {
        next_socket = 0;
    }

    return 0;
}

void mem_exit(int status)
{
    ExitException e;
    e.status = status;
    throw e;
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
    try
    {
        lora_pkt_fwd_main();
    }
    catch (ExitException &e)
    {
        return e.status;
    }
    return EXIT_SUCCESS;
}

void stop()
{
    signal_handler(SIGTERM);
}

ssize_t recv_from(int link,
                  void *buf, size_t len,
                  const struct timeval *timeout)
{
    return links[link].from_fwd_recv(buf, len, to_microseconds(timeout));
}

ssize_t send_to(int link,
                const void *buf, size_t len,
                ssize_t hwm, const struct timeval *timeout)
{
    return links[link].to_fwd_send(buf, len, hwm, to_microseconds(timeout));
}

void set_gw_send_hwm(int link, const ssize_t hwm)
{
    links[link].set_from_fwd_send_hwm(hwm);
}

void set_gw_send_timeout(int link, const struct timeval *timeout)
{
    links[link].set_from_fwd_send_timeout(to_microseconds(timeout));
}

void set_gw_recv_timeout(int link, const struct timeval *timeout)
{
    links[link].set_to_fwd_recv_timeout(to_microseconds(timeout));
}

}