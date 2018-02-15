#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <queue>
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <string>

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
        closed = false;
    }

    void close()
    {
        std::unique_lock<std::mutex> lock(m);
        queue_t empty;
        std::swap(q, empty);
        size = 0;
        closed = true;
        send_cv.notify_all();
        recv_cv.notify_all();
    }

    ssize_t send(const void *buf, size_t len,
                 ssize_t hwm, const Duration &timeout)
    {
        std::unique_lock<std::mutex> lock(m);

        if (closed)
        {
            errno = EBADF;
            return -1;
        }

        if (hwm == 0)
        {
            return 0;
        }

        if ((hwm > 0) && (size >= hwm))
        {
            // wait until buffered data size < hwm
            auto pred = [this, hwm]
            {
                return closed || (this->size < hwm);
            };

            if (timeout < Duration::zero())
            {
                // timeout < 0 means block
                send_cv.wait(lock, pred);
            }
            else if ((timeout == Duration::zero()) ||
                     !send_cv.wait_for(lock, timeout, pred))
            {
                errno = EAGAIN;
                return -1;
            }

            if (closed)
            {
                errno = EBADF;
                return -1;
            }
        }

        auto bytes = static_cast<const uint8_t*>(buf);
        q.emplace(bytes, &bytes[len]);
        size += len;
        recv_cv.notify_all();

        return len;
    }

    ssize_t recv(void *buf, size_t len, const Duration &timeout)
    {
        std::unique_lock<std::mutex> lock(m);

        if (closed)
        {
            errno = EBADF;
            return -1;
        }

        if (q.empty())
        {
            auto pred = [this]
            {
                return closed || !this->q.empty();
            };

            if (timeout < Duration::zero())
            {
                // timeout < 0 means block
                recv_cv.wait(lock, pred);
            }
            else if ((timeout == Duration::zero()) ||
                     !recv_cv.wait_for(lock, timeout, pred))
            {
                errno = EAGAIN;
                return -1;
            }

            if (closed)
            {
                errno = EBADF;
                return -1;
            }
        }

        element_t &el = q.front();
        ssize_t r = std::min(el.size(), len);
        memcpy(buf, el.data(), r);
        q.pop();
        size -= el.size();
        send_cv.notify_all();

        return r;
    }

private:
    std::mutex m;
    std::condition_variable send_cv, recv_cv;
    queue_t q;
    ssize_t size = 0;
    bool closed = false;
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

    void close()
    {
        from_fwd.close();
        to_fwd.close();
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

static int next_socket = uplink;
static Link links[2];
static sighandler_t signal_handler = nullptr;
static bool signal_handler_called = false;
static bool stop_requested = false;
std::mutex stop_mutex;
static std::string cfg_prefix;
static std::atomic<logger_fn> logger(nullptr);

struct ExitException : public std::exception
{
    int status;
};

struct StartAndArg
{
    void *(*start_routine)(void*);
    void *arg;
};

static std::chrono::microseconds to_microseconds(const struct timeval *tv)
{
    return tv ? (tv->tv_sec * 1s + tv->tv_usec * 1us) : -1us;
}

static void check_stop(sighandler_t handler, bool request_stop)
{
    sighandler_t h = nullptr;
    {
        std::unique_lock<std::mutex> lock(stop_mutex);

        if (handler)
        {
            signal_handler = handler;
        }

        if (request_stop)
        {
            stop_requested = true;
        }

        if (signal_handler && stop_requested && !signal_handler_called)
        {
            h = signal_handler;
            signal_handler_called = true;
        }
    }

    if (h)
    {
        h(SIGTERM);
    }
}

static void *with_catcher(void *arg)
{
    try
    {
        auto arg2 = static_cast<StartAndArg*>(arg);
        auto start_routine = arg2->start_routine;
        arg = arg2->arg;
        delete arg2;
        return start_routine(arg);
    }
    catch (ExitException &e)
    {
        check_stop(nullptr, true);
        return nullptr;
    }
}

static int log(FILE* stream, const char *format, va_list ap)
{
    logger_fn logf = logger;
    int r = logf ? logf(stream, format, ap) : 0;
    va_end(ap);
    return r;
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

    return 0;
}

void mem_exit(int status)
{
    ExitException e;
    e.status = status;
    throw e;
}

int mem_pthread_create(pthread_t *thread,
                       const pthread_attr_t *attr,
                       void *(*start_routine)(void*),
                       void *arg)
{
    auto arg2 = new StartAndArg();
    arg2->start_routine = start_routine;
    arg2->arg = arg;
    return pthread_create(thread, attr, with_catcher, arg2);
}

int mem_pthread_cancel(pthread_t thread)
{
    return pthread_join(thread, NULL);
}

void mem_sigaction(int signum,
                   const struct sigaction *act,
                   struct sigaction*)
{
    if ((signum == SIGTERM) && act)
    {
        check_stop(act->sa_handler, false);
    }
}

int mem_access(const char *pathname, int mode)
{
    return access((cfg_prefix + pathname).c_str(), mode);
}

FILE *mem_fopen(const char *pathname, const char *mode)
{
    return fopen((cfg_prefix + pathname).c_str(), mode);
}

void mem_wait_ms(unsigned long a)
{
    struct timespec dly, slp;

    dly.tv_sec = a / 1000;
    dly.tv_nsec = (static_cast<long>(a) % 1000) * 1000000;

    while ((dly.tv_sec > 0) || ((dly.tv_sec == 0) && (dly.tv_nsec > 100000)))
    {
        {
            std::unique_lock<std::mutex> lock(stop_mutex);
            if (signal_handler_called)
            {
                break;
            }
        }

        slp.tv_sec = std::min(dly.tv_sec, static_cast<time_t>(1));
        slp.tv_nsec = dly.tv_nsec;

        if (clock_nanosleep(CLOCK_MONOTONIC, 0, &slp, NULL) != 0)
        {
            // original function doesn't loop on EINTR
            break;
        }

        dly.tv_sec -= slp.tv_sec;
        dly.tv_nsec = 0;
    }
}

ssize_t mem_read(int fd, void *buf, size_t count)
{
    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(stop_mutex);
            if (signal_handler_called)
            {
                return 0;
            }
        }

        if (poll(&fds, 1, 1000) > 0)
        {
            return read(fd, buf, count);
        }
    }
}

int mem_printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    return log(stdout, format, ap);
}

int mem_fprintf(FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    return log(stream, format, ap);
}

int mem_printf_chk(int, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    return log(stdout, format, ap);
}

int mem_fprintf_chk(FILE *stream, int, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    return log(stream, format, ap);
}

int start(const char *cfg_dir)
{
    int r = EXIT_SUCCESS;

    if (cfg_dir)
    {
        cfg_prefix = cfg_dir;
        cfg_prefix += "/";
    }
    else
    {
        cfg_prefix = "";
    }

    try
    {
        lora_pkt_fwd_main();
    }
    catch (ExitException &e)
    {
        r = e.status;
    }

    links[uplink].close();
    links[downlink].close();

    return r;
}

void stop()
{
    check_stop(nullptr, true);
}

void reset()
{
    next_socket = uplink;
    links[uplink].reset();
    links[downlink].reset();
    signal_handler = nullptr;
    signal_handler_called = false;
    stop_requested = false;
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

void set_logger(logger_fn f)
{
    logger = f;
}

}
