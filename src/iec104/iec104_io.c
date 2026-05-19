#include "iec104/iec104_io.h"

#include "logging/logging.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* 将 socket 设置为非阻塞模式，用于实现连接超时。 */
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* 将 socket 恢复为阻塞模式，便于后续按帧读写。 */
static int set_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

/* 对指定 socket 地址执行带超时的 TCP 连接。 */
static int connect_sockaddr_with_timeout(const struct sockaddr *address,
                                         socklen_t address_len,
                                         int family,
                                         int timeout_sec)
{
    int fd = -1;
    int rc;
    int opt = 1;
    struct pollfd pfd;
    int error = 0;
    socklen_t error_len = sizeof(error);

    fd = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

    if (set_nonblocking(fd) != 0) {
        close(fd);
        return -1;
    }

    rc = connect(fd, address, address_len);
    if (rc == 0) {
        set_blocking(fd);
        return fd;
    }

    if (errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    rc = poll(&pfd, 1, timeout_sec * 1000);
    if (rc > 0 && (pfd.revents & POLLOUT) != 0 &&
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_len) == 0 && error == 0) {
        set_blocking(fd);
        return fd;
    }

    close(fd);
    return -1;
}

/* 设置连接成功后的 socket 收发超时。 */
static void set_socket_timeouts(int fd, int timeout_sec)
{
    struct timeval timeout;

    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

int iec104_connect_with_timeout(const gateway_config_t *gateway, int timeout_sec)
{
    struct sockaddr_in ipv4_address;
    struct sockaddr_in6 ipv6_address;
    const char *host = gateway->slave_host;
    uint16_t port = gateway->slave_port;
    int fd = -1;

    memset(&ipv4_address, 0, sizeof(ipv4_address));
    ipv4_address.sin_family = AF_INET;
    ipv4_address.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &ipv4_address.sin_addr) == 1) {
        fd = connect_sockaddr_with_timeout((struct sockaddr *)&ipv4_address,
                                           sizeof(ipv4_address),
                                           AF_INET,
                                           timeout_sec);
        if (fd >= 0) {
            set_socket_timeouts(fd, timeout_sec);
        }
        return fd;
    }

    memset(&ipv6_address, 0, sizeof(ipv6_address));
    ipv6_address.sin6_family = AF_INET6;
    ipv6_address.sin6_port = htons(port);
    if (inet_pton(AF_INET6, host, &ipv6_address.sin6_addr) == 1) {
        fd = connect_sockaddr_with_timeout((struct sockaddr *)&ipv6_address,
                                           sizeof(ipv6_address),
                                           AF_INET6,
                                           timeout_sec);
        if (fd >= 0) {
            set_socket_timeouts(fd, timeout_sec);
        }
        return fd;
    }

    {
        struct addrinfo hints;
        struct addrinfo *addresses = NULL;
        char port_text[16];
        int gai_rc;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        snprintf(port_text, sizeof(port_text), "%u", port);

        gai_rc = getaddrinfo(host, port_text, &hints, &addresses);
        if (gai_rc != 0) {
            log_write(LOG_LEVEL_ERROR,
                      "[%s] resolve gateway %s:%u failed: %s",
                      gateway->name,
                      host,
                      port,
                      gai_strerror(gai_rc));
            return -1;
        }

        for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next) {
            fd = connect_sockaddr_with_timeout(address->ai_addr,
                                               address->ai_addrlen,
                                               address->ai_family,
                                               timeout_sec);
            if (fd >= 0) {
                set_socket_timeouts(fd, timeout_sec);
                break;
            }
        }

        freeaddrinfo(addresses);
    }

    return fd;
}

int iec104_write_all(int fd, const uint8_t *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t rc = send(fd, buffer + sent, length - sent, MSG_NOSIGNAL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        sent += (size_t)rc;
    }

    return 0;
}

/* 从 socket 读取指定长度数据，遇到断线或超时返回对应状态。 */
static int receive_exact(int fd, uint8_t *buffer, size_t length)
{
    size_t received = 0;

    while (received < length) {
        ssize_t rc = recv(fd, buffer + received, length - received, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 1;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        received += (size_t)rc;
    }

    return 0;
}

int iec104_wait_and_read_frame(const gateway_config_t *gateway,
                               int fd,
                               uint8_t *frame,
                               size_t *frame_len,
                               int timeout_sec,
                               volatile sig_atomic_t *running)
{
    fd_set readfds;
    struct timeval timeout;
    int rc;
    uint8_t header[2];

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    rc = select(fd + 1, &readfds, NULL, NULL, &timeout);
    if (rc < 0) {
        if (errno == EINTR) {
            return *running ? 1 : -1;
        }
        return -1;
    }
    if (rc == 0) {
        return 1;
    }

    rc = receive_exact(fd, header, sizeof(header));
    if (rc != 0) {
        return rc;
    }

    if (header[0] != IEC104_START || header[1] < 4 || header[1] > IEC104_MAX_APDU - 2) {
        log_write(LOG_LEVEL_WARN, "[%s] invalid frame header start=0x%02x len=%u", gateway->name, header[0], header[1]);
        return -1;
    }

    frame[0] = header[0];
    frame[1] = header[1];
    rc = receive_exact(fd, frame + 2, header[1]);
    if (rc != 0) {
        log_write(LOG_LEVEL_WARN, "[%s] incomplete IEC104 frame body len=%u", gateway->name, header[1]);
        return -1;
    }

    *frame_len = (size_t)header[1] + 2;
    return 0;
}
