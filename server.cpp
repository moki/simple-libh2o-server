#define H2O_USE_LIBUV 0

#include "h2o.h"
#include "h2o/http1.h"
#include "h2o/http2.h"
#include <cstring>
#include <errno.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>

static h2o_globalconf_t config;
static h2o_context_t ctx;
static h2o_accept_ctx_t accept_ctx;

static void on_accept(h2o_socket_t *listener, const char *err) {
        h2o_socket_t *sock;
        if (err != NULL)
                return;
        if ((sock = h2o_evloop_socket_accept(listener)) == NULL)
                return;
        h2o_accept(&accept_ctx, sock);
}

static int listener(void) {
        struct sockaddr_in addr;
        int fd, r = 1;
        h2o_socket_t *sock;

        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        if (!inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr))
                return -1;
        addr.sin_port = htons(3000);

        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &r, sizeof(r)) != 0 ||
            bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
            listen(fd, SOMAXCONN) != 0) {
                return -1;
        }

        sock = h2o_evloop_socket_create(ctx.loop, fd,
                                        H2O_SOCKET_FLAG_DONT_READ);
        h2o_socket_read_start(sock, on_accept);

        return 0;
}

static h2o_pathconf_t *
register_handler(h2o_hostconf_t *host_conf, const char *path,
                 int (*on_req)(h2o_handler_t *, h2o_req_t *)) {
        h2o_pathconf_t *path_conf =
                h2o_config_register_path(host_conf, path, 0);
        h2o_handler_t *handler =
                h2o_create_handler(path_conf, sizeof(*handler));
        handler->on_req = on_req;
        return path_conf;
}

static int hello_handler(h2o_handler_t *self, h2o_req_t *req) {
        if (!h2o_memis(req->method.base, req->method.len, H2O_STRLIT("GET")))
                return -1;
        req->res.status = 200;
        req->res.reason = "OK";
        h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE,
                       NULL, H2O_STRLIT("text/plain"));
        h2o_send_inline(req, H2O_STRLIT("Hello, world\n"));
        return 0;
}

int main() {
        /* mask sigpipe */
        struct sigaction n, o;
        n.sa_handler = SIG_IGN;
        sigemptyset(&n.sa_mask);
        n.sa_flags = 0;

        sigaction(SIGPIPE, NULL, &o);
        sigaction(SIGPIPE, &n, NULL);

        std::memset((void *)&config, 0, sizeof(config));
        std::memset((void *)&ctx, 0, sizeof(ctx));
        std::memset((void *)&accept_ctx, 0, sizeof(accept_ctx));

        h2o_pathconf_t *path_conf;

        /* host config */
        h2o_config_init(&config);
        h2o_iovec_t default_host = h2o_iovec_init(H2O_STRLIT("default"));
        h2o_hostconf_t *host_conf =
                h2o_config_register_host(&config, default_host, 65535);

        /* routes */
        path_conf = register_handler(host_conf, "/sayhello", hello_handler);

        /* server static assets */
        path_conf = h2o_config_register_path(host_conf, "/", 0);
        h2o_compress_args_t ca;
        h2o_compress_register(path_conf, &ca);
        h2o_file_register(path_conf, "static", NULL, NULL,
                          H2O_FILE_FLAG_GUNZIP);

        /* event loop */
        h2o_context_init(&ctx, h2o_evloop_create(), &config);
        accept_ctx.ctx = &ctx;
        accept_ctx.hosts = config.hosts;

        if (listener() != 0) {
                std::cout << "failed to startup server at 127.0.0.1:3000"
                          << std::endl;
                goto errexit;
        } else
                std::cout << "server listens at 127.0.0.1:3000" << std::endl;

        for (; h2o_evloop_run(ctx.loop, INT32_MAX) == 0;)
                ;

        return 0;
errexit:
        exit(1);
}
