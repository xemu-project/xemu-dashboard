#define _CRT_RAND_S
#include <stdlib.h>

#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

void mbedtls_net_init(mbedtls_net_context *ctx)
{
    (void)ctx;
}

int mbedtls_net_connect(mbedtls_net_context *ctx, const char *host,
                        const char *port, int proto)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    struct addrinfo hints = {0};
    struct addrinfo *addr_info;

    hints.ai_family = AF_INET;
    hints.ai_socktype = proto == MBEDTLS_NET_PROTO_UDP ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = proto == MBEDTLS_NET_PROTO_UDP ? IPPROTO_UDP : IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &addr_info) != 0) {
        return MBEDTLS_ERR_NET_UNKNOWN_HOST;
    }

    for (struct addrinfo *cur = addr_info; cur != NULL; cur = cur->ai_next) {
        ctx->fd = (int)socket(cur->ai_family, cur->ai_socktype,
                              cur->ai_protocol);
        if (ctx->fd < 0) {
            ret = MBEDTLS_ERR_NET_SOCKET_FAILED;
            continue;
        }

        if (connect(ctx->fd, cur->ai_addr, cur->ai_addrlen) != 0) {
            ret = MBEDTLS_ERR_NET_CONNECT_FAILED;
            close(ctx->fd);
            ctx->fd = -1;
            continue;
        }

        ret = 0;
        break;
    }

    freeaddrinfo(addr_info);

    return (ret);
}

int mbedtls_net_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = ((mbedtls_net_context *)ctx)->fd;
    return send(fd, buf, len, 0);
}

int mbedtls_net_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = ((mbedtls_net_context *)ctx)->fd;
    return recv(fd, buf, len, 0);
}

void mbedtls_net_free(mbedtls_net_context *ctx)
{
    close(ctx->fd);
    ctx->fd = -1;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    if (len < 4) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    size_t written = 0;
    while (written < len) {
        rand_s((unsigned int *)output);
        output += 4;
        written += 4;
    }

    *olen = written;
    return 0;
}
