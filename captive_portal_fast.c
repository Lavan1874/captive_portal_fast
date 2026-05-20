#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PORT 44381
#define BACKLOG 128
#define MAX_CLIENTS 1024
#define CLIENT_TIMEOUT_MS 5000
#define REQ_BUF_SIZE 1024

struct client {
  int fd;
  char req[REQ_BUF_SIZE];
  size_t len;
  long long deadline_ms;
  int responded;
};

static const char R204_KEEP[] =
    "HTTP/1.1 204 No Content\r\n"
    "Connection: keep-alive\r\n"
    "Keep-Alive: timeout=5\r\n"
    "\r\n";

static long long monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void send_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = send(fd, buf, len, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      return;
    }
    if (n == 0) return;
    buf += n;
    len -= (size_t)n;
  }
}

static int ascii_tolower(int c) {
  if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
  return c;
}

static int contains_ci(const char *buf, size_t n, const char *needle) {
  size_t needle_len = strlen(needle);
  if (needle_len == 0 || n < needle_len) return 0;

  for (size_t i = 0; i <= n - needle_len; i++) {
    size_t j = 0;
    while (j < needle_len &&
           ascii_tolower((unsigned char)buf[i + j]) ==
               ascii_tolower((unsigned char)needle[j])) {
      j++;
    }
    if (j == needle_len) return 1;
  }

  return 0;
}

static int headers_complete(const char *req, size_t n, size_t *header_end) {
  for (size_t i = 0; i + 3 < n; i++) {
    if (req[i] == '\r' && req[i + 1] == '\n' && req[i + 2] == '\r' &&
        req[i + 3] == '\n') {
      *header_end = i + 4;
      return 1;
    }
  }

  for (size_t i = 0; i + 1 < n; i++) {
    if (req[i] == '\n' && req[i + 1] == '\n') {
      *header_end = i + 2;
      return 1;
    }
  }

  return 0;
}

static int should_close_after_response(const char *req, size_t n) {
  return contains_ci(req, n, "\nconnection: close") ||
         contains_ci(req, n, "\r\nconnection: close") ||
         contains_ci(req, n, " HTTP/1.0\r") ||
         contains_ci(req, n, " HTTP/1.0\n");
}

static void send_probe_response(struct client *client) {
  if (client->responded) return;
  send_all(client->fd, R204_KEEP, sizeof(R204_KEEP) - 1);
  client->responded = 1;
}

static int discard_complete_requests(struct client *client) {
  for (;;) {
    size_t header_end = 0;
    if (!headers_complete(client->req, client->len, &header_end)) {
      if (client->len == sizeof(client->req)) {
        client->len = 0;
      }
      return 0;
    }

    if (should_close_after_response(client->req, header_end)) return 1;

    size_t remaining = client->len - header_end;
    memmove(client->req, client->req + header_end, remaining);
    client->len = remaining;
    client->responded = 0;
    client->deadline_ms = monotonic_ms() + CLIENT_TIMEOUT_MS;

    if (remaining == 0) return 0;
    send_probe_response(client);
  }
}

static int read_client(struct client *client) {
  for (;;) {
    ssize_t n =
        read(client->fd, client->req + client->len, sizeof(client->req) - client->len);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
      return 1;
    }
    if (n == 0) return 1;

    client->len += (size_t)n;
    client->deadline_ms = monotonic_ms() + CLIENT_TIMEOUT_MS;

    send_probe_response(client);
    if (discard_complete_requests(client)) return 1;
  }
}

static void close_client(struct client *clients, size_t *nclients, size_t i) {
  close(clients[i].fd);
  clients[i] = clients[*nclients - 1];
  (*nclients)--;
}

static void accept_clients(int server, struct client *clients, size_t *nclients) {
  for (;;) {
    int fd = accept(server, NULL, NULL);
    if (fd < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      perror("accept");
      return;
    }

    if (*nclients == MAX_CLIENTS || set_nonblocking(fd) < 0) {
      close(fd);
      continue;
    }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    clients[*nclients].fd = fd;
    clients[*nclients].len = 0;
    clients[*nclients].deadline_ms = monotonic_ms() + CLIENT_TIMEOUT_MS;
    clients[*nclients].responded = 0;
    (*nclients)++;
  }
}

int main(void) {
  int one = 1;
  int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) {
    perror("socket");
    return 1;
  }

  signal(SIGPIPE, SIG_IGN);

  if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
    perror("setsockopt");
    close(server);
    return 1;
  }

  if (set_nonblocking(server) < 0) {
    perror("fcntl");
    close(server);
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(server);
    return 1;
  }

  if (listen(server, BACKLOG) < 0) {
    perror("listen");
    close(server);
    return 1;
  }

  struct client clients[MAX_CLIENTS];
  size_t nclients = 0;

  for (;;) {
    struct pollfd fds[MAX_CLIENTS + 1];
    fds[0].fd = server;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    for (size_t i = 0; i < nclients; i++) {
      fds[i + 1].fd = clients[i].fd;
      fds[i + 1].events = POLLIN;
      fds[i + 1].revents = 0;
    }

    size_t polled_clients = nclients;
    int nready = poll(fds, (nfds_t)nclients + 1, 1000);
    if (nready < 0) {
      if (errno == EINTR) continue;
      perror("poll");
      continue;
    }

    for (size_t i = 0; i < polled_clients; i++) {
      short revents = fds[i + 1].revents;
      if (revents == 0) continue;

      size_t client_i = 0;
      while (client_i < nclients && clients[client_i].fd != fds[i + 1].fd) {
        client_i++;
      }
      if (client_i == nclients) continue;

      int done = 0;
      if (revents & POLLIN) done = read_client(&clients[client_i]);
      if (!done && (revents & (POLLERR | POLLHUP | POLLNVAL))) done = 1;
      if (done) close_client(clients, &nclients, client_i);
    }

    long long now = monotonic_ms();
    for (size_t i = 0; i < nclients;) {
      if (now > clients[i].deadline_ms) {
        close_client(clients, &nclients, i);
      } else {
        i++;
      }
    }

    if (fds[0].revents & POLLIN) {
      accept_clients(server, clients, &nclients);
    }
  }
}
