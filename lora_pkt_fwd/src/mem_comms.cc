#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
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

using namespace std::chrono_literals;

const int SOCKET_UP = 0,
          SOCKET_DOWN = 1;

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

    ssize_t write(const void *buf, size_t len,
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
            else if (!cv.wait_for(lock, timeout, pred))
            {
                errno = EAGAIN;
                return -1;
            }
        }

        auto bytes = static_cast<const uint8_t*>(buf);
        q.emplace(bytes, &bytes[len]);
        size += len;

        // TODO: what about dropping if queue too big?

        return 0;
    }

    ssize_t read(void *buf, size_t len, const Duration &timeout)
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
            else if (!cv.wait_for(lock, timeout, pred))
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
        to_fwd_read_timeout = -1us;
        from_fwd.reset();
        to_fwd.reset();
    }

    void set_to_fwd_read_timeout(const struct timeval &timeout)
    {
        if (timerisset(&timeout))
        {
            to_fwd_read_timeout = timeout.tv_sec * 1s + timeout.tv_usec * 1us;
        }
        else
        {
            // setsockopt uses 0 to block
            to_fwd_read_timeout = -1us;
        }
    }

    ssize_t from_fwd_write(const void *buf, size_t len)
    {
        return from_fwd.write(buf, len, -1, -1us);
    }

    ssize_t from_fwd_read(void *buf, size_t len,
                          const std::chrono::microseconds &timeout)
    {
        return from_fwd.read(buf, len, timeout);
    }

    ssize_t to_fwd_write(const void *buf, size_t len,
                         ssize_t hwm, const std::chrono::microseconds &timeout)
    {
        return to_fwd.write(buf, len, hwm, timeout);
    }

    ssize_t to_fwd_read(void *buf, size_t len)
    {
        return to_fwd.read(buf, len, to_fwd_read_timeout);
    }

private:
    std::chrono::microseconds to_fwd_read_timeout = -1us;
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
    if ((sockfd < 0) || (sockfd > SOCKET_DOWN))
    {
        errno = EBADF;
        return -1;
    }

    return 0;
}

int mem_setsockopt(int sockfd, int level, int optname,
                   const void* optval, socklen_t optlen)
{
    if ((sockfd < 0) || (sockfd > SOCKET_DOWN))
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

    links[sockfd].set_to_fwd_read_timeout(
        *static_cast<const struct timeval*>(optval));

    return 0;
}

ssize_t mem_send(int sockfd, const void *buf, size_t len, int /*flags*/)
{
    if ((sockfd < 0) || (sockfd > SOCKET_DOWN))
    {
        errno = EBADF;
        return -1;
    }

    return links[sockfd].from_fwd_write(buf, len);
}

ssize_t mem_recv(int sockfd, void *buf, size_t len, int /*flags*/)
{
    if ((sockfd < 0) || (sockfd > SOCKET_DOWN))
    {
        errno = EBADF;
        return -1;
    }

    return links[sockfd].to_fwd_read(buf, len);
}

int mem_shutdown(int sockfd, int)
{
    if ((sockfd < 0) || (sockfd > SOCKET_DOWN))
    {
        errno = EBADF;
        return -1;
    }

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

// TODO
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
