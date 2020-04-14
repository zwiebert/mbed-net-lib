#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"

#include "time.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "net/http/server/http_server.h"
#include "net/tcp_cli_server.h"
#include "cli/cli.h"
#include "cli/mutex.h"
#include "tcp_cli_server.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <errno.h>
#include "txtio/inout.h"

#include "tcp_cli_server.h"
#include "misc/int_types.h"

#include <sys/select.h>

#define TAG "tcps"

#define TCP_HARD_TIMEOUT  (60 * 10)  // terminate connections to avoid dead connections piling up
#define SERIAL_ECHO 1
#define SERIAL_INPUT 1
#define PUTC_LINE_BUFFER 1

#define printf ets_printf
#ifndef DISTRIBUTION
#define DP(x) printf("db:tcps: %s\n", (x))
#define D(x) x
#else
#define D(x)
#endif

#ifndef TCPS_TASK_PORT
#define TCPS_TASK_PORT   7777
#endif
#define MAX_BUF   1024
#define TCPS_CCONN_MAX 5

typedef void (*fd_funT)(int fd, void *args);
int foreach_fd(fd_set *fdsp, int count, fd_funT fd_fun, void *args);
static void modify_io_fun(bool add_connection);

static int sockfd = -1;
fd_set wait_fds;
int nfds;
static struct sockaddr_in self;
static int cconn_count;
static int (*old_io_putc_fun)(char c);
static int (*old_io_getc_fun)(void);

const unsigned cli_buf_size = 120;
char *cli_buf;


static void set_sockfd(int fd) {
  if (fd + 1 > nfds)
    nfds = fd + 1;
  sockfd = fd;
}

static void add_fd(int fd) {
  FD_SET(fd, &wait_fds);
  if (fd + 1 > nfds)
    nfds = fd + 1;
  ++cconn_count;
  modify_io_fun(true);
}

static void rm_fd(int fd) {
  FD_CLR(fd, &wait_fds);
  if (fd - 1 == nfds)
    --nfds;

  if (--cconn_count < 0)
    cconn_count = 0;
  modify_io_fun(false);
}

static void tcps_close_cconn(int fd) {
  if (lwip_close(fd) < 0) {
    perror("close");
    return;
  }

  rm_fd(fd);
  printf("tcps: disconnected. %d client(s) still connected\n", cconn_count);
}

int foreach_fd(fd_set *fdsp, int count, fd_funT fd_fun, void *args) {
  for (int i = 0; count && i < nfds; ++i) {
    if (FD_ISSET(i, fdsp)) {
      (*fd_fun)(i, args);
      --count;
    }
  }
  return count;
}

static int tcps_create_server() {
  int fd;
  /** Create streaming socket */
  if ((fd = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Socket");
    return (errno);
  }
  if (lwip_fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl");
    goto err;
  }
  /** Initialize address/port structure */
  bzero(&self, sizeof(self));
  self.sin_family = AF_INET;
  self.sin_port = htons(TCPS_TASK_PORT);
  self.sin_addr.s_addr = INADDR_ANY;

  /** Assign a port number to the socket */
  if (lwip_bind(fd, (struct sockaddr*) &self, sizeof(self)) != 0) {
    perror("socket:bind()");
    goto err;
  }

  /** Make it a "listening socket". Limit to 16 connections */
  if (lwip_listen(fd, TCPS_CCONN_MAX) != 0) {
    perror("socket:listen()");
    goto err;
  }

  set_sockfd(fd);
  return 0;

  err: lwip_close(fd);
  return (errno);
}

void tcps_task_write(int fd, const char *txt) {
  if (cconn_count <= 0) {
    D(printf("%s: no open connections\n", __func__));
    return;
  }

  if (fd >= 0) {
    lwip_write(fd, txt, strlen(txt));
    return;
  }

  if (fd == -2)
    for (fd = 0; fd < nfds; ++fd) {
      if (!FD_ISSET(fd, &wait_fds))
        continue;
      lwip_write(fd, txt, strlen(txt));
    }
}

static void tcpst_putc(int fd, void *c) {
  lwip_write(fd, c, 1);
}

static void tcpst_putc_all(char c) {
  foreach_fd(&wait_fds, nfds, tcpst_putc, &c);
}

static int  tcp_io_putc(char c) {
  if (cconn_count > 0)
    tcpst_putc_all(c);
  (*old_io_putc_fun)(c);
  return 1;
}

static void modify_io_fun(bool add_connection) {
  if (add_connection && cconn_count == 1) {
    // fist connection opened
    printf("modify io to tcp\n");
  //  old_io_getc_fun = io_getc_fun;
    old_io_putc_fun = io_putc_fun;
   // io_getc_fun = tcp_io_getc;
    io_putc_fun = tcp_io_putc;
  } else if (cconn_count == 0) {
    // last connection closed
    printf("modify io to serial\n");
 //   io_getc_fun = old_io_getc_fun;
    io_putc_fun = old_io_putc_fun;
  }
}

static int selected_fd;
static int tcps_getc() {
  int result = -1;
  static int fd = -1;
  if (selected_fd >= 0) {
    fd = selected_fd;
  }
  char c;
  int n = lwip_recv(fd, &c, 1, MSG_DONTWAIT);

  if (n == 0) {
    if (selected_fd >= 0) {
      // remote socket was closed
      tcps_close_cconn(fd);
    }
  } else if (n == 1) {
    result = c;
  } else {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {

    } else {
      rm_fd(fd);
    }
  }

  selected_fd = -1;
  return result;
}

static void try_accept() {
  int fd;
  struct sockaddr_in client_addr;
  size_t addrlen = sizeof(client_addr);

  /** accept an incomming connection  */
  fd = lwip_accept(sockfd, (struct sockaddr*) &client_addr, &addrlen);
  if (fd >= 0) {
    add_fd(fd);
    printf("%s:%d connected (%d clients)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), cconn_count);
  } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
    perror("tcps: accept");
  }
}

void handle_input(int fd, void *args) {
  selected_fd = fd;
  char *cmdline = cli_get_commandline(cli_buf, cli_buf_size, tcps_getc);
  if (cmdline) {
    if (mutex_cliTake()) {
      if (cmdline[0] == '{') {
        cli_process_json(cmdline, SO_TGT_CLI);
      } else {
        cli_process_cmdline(cmdline, SO_TGT_CLI);
      }
      mutex_cliGive();
    }
  }
}



void wait_for_fd() {
  fd_set rfds = wait_fds, wfds = wait_fds;
  struct timeval tv = { .tv_sec = 10 };

  FD_SET(sockfd, &rfds);

  int count = lwip_select(nfds, &rfds, NULL, NULL, NULL);
  if (count < 0) {
    return;
  }
  if (count == 0) {
    return;
  }
  D(printf("select returned. count=%d\n", count));

  if (FD_ISSET(sockfd, &rfds)) {
    try_accept();

    FD_CLR(sockfd, &rfds);
    --count;
  }
  count = foreach_fd(&rfds, count, handle_input, 0);

}

static void tcps_task(void *pvParameters) {

  FD_ZERO(&wait_fds);
  nfds = 0;

  if (tcps_create_server() == 0) {
    ESP_LOGI(TAG, "tcp server created");
  }
  for (;;) {
    wait_for_fd();
  }
}

static TaskHandle_t xHandle = NULL;
#define STACK_SIZE  2000
void tcpCli_setup_task(const struct cfg_tcps *cfg_tcps) {
  static uint8_t ucParameterToPass;

  if (!cfg_tcps || !cfg_tcps->enable) {
    if (xHandle) {
      vTaskDelete(xHandle);
      xHandle = NULL;
      free(cli_buf);
      cli_buf = 0;
    }
    return;
  }

  cli_buf = calloc(cli_buf_size, 1);
  xTaskCreate(tcps_task, "tcp_server", STACK_SIZE, &ucParameterToPass, tskIDLE_PRIORITY, &xHandle);
  configASSERT( xHandle );

}

