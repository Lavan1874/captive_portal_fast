#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 44380
#define BACKLOG 128
#define REQ_BUF_SIZE 1024
#define STARTS_WITH(buf, n, lit) \
  ((n) >= (ssize_t)(sizeof(lit) - 1) && memcmp((buf), (lit), sizeof(lit) - 1) == 0)

static const char R204[] =
    "HTTP/1.1 204 No Content\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char R200[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: 33\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body>Success</body></html>";

static const char R200_HEAD[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: 33\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char R404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Not Found";

static void send_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = send(fd, buf, len, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return;
    }
    if (n == 0) return;
    buf += n;
    len -= (size_t)n;
  }
}

static void handle_client(int fd) {
  char req[REQ_BUF_SIZE];
  ssize_t n = read(fd, req, sizeof(req));

  if (n <= 0) return;

  if (STARTS_WITH(req, n, "GET /generate_204 ") ||
      STARTS_WITH(req, n, "HEAD /generate_204 ")) {
    send_all(fd, R204, sizeof(R204) - 1);
  } else if (STARTS_WITH(req, n, "GET /success.html ")) {
    send_all(fd, R200, sizeof(R200) - 1);
  } else if (STARTS_WITH(req, n, "HEAD /success.html ")) {
    send_all(fd, R200_HEAD, sizeof(R200_HEAD) - 1);
  } else {
    send_all(fd, R404, sizeof(R404) - 1);
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

  for (;;) {
    int client = accept(server, NULL, NULL);
    if (client < 0) {
      if (errno == EINTR) continue;
      perror("accept");
      continue;
    }

    handle_client(client);
    close(client);
  }
}
