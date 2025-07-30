#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/epoll.h> 
#include <fcntl.h>      

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096

static void msg(const char *msg) {
  fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
} 

// Non blocking socket
static void set_nonblocking(int fd) {
  int flags = fctnl(fd, F_GETFL, 0);
  if (flags == -1) {
    die("fcntl F_GETFL");
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == - 1) {
    die("fcntl F_SETFL O_NONBLOCK");
  }
}

static void handle_new_connection(int listen_fd, int epoll_fd) {
  struct sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);

  int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
  if (conn_fd == -1) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      msg("accept() error");
    }
    return;
  }

  set_nonblocking(conn_fd);
    
    // Add client socket to epoll for monitoring
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;  // Edge-triggered for efficiency
    ev.data.fd = conn_fd;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
        msg("epoll_ctl: conn_fd");
        close(conn_fd);
        return;
    }
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    fprintf(stderr, "New connection from %s:%d (fd=%d)\n", 
            client_ip, ntohs(client_addr.sin_port), conn_fd);
}

// Handle data from existing client
static void handle_client_data(int client_fd, int epoll_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    // Read all available data (edge-triggered epoll)
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';  // Null terminate for printing
        fprintf(stderr, "Client fd=%d says: %s", client_fd, buffer);
        
        // Echo back the data
        const char response[] = "world\n";
        ssize_t bytes_written = write(client_fd, response, strlen(response));
        
        if (bytes_written == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                msg("write() error");
                goto cleanup;
            } 
            break;
        }
    }
     
    if (bytes_read == 0) {
        fprintf(stderr, "Client fd=%d disconnected\n", client_fd);
        goto cleanup;
    } else if (bytes_read == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            msg("read() error");
            goto cleanup;
        } 
        return;
    }
    
    return;

cleanup: 
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        die("socket()");
    }
   
    int val = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
        die("setsockopt SO_REUSEADDR");
    }
     
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);        // HOST to NETWORK byte order
    addr.sin_addr.s_addr = htonl(0);    // 0.0.0.0 wildcard address
    
    if (bind(listen_fd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
        die("bind()");
    }
     
    if (listen(listen_fd, SOMAXCONN) == -1) {
        die("listen()");
    }
    
    set_nonblocking(listen_fd);
    
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        die("epoll_create1()");
    }
    
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        die("epoll_ctl: listen_fd");
    }
    
    fprintf(stderr, "Server listening on 0.0.0.0:1234\n");
    
    struct epoll_event events[MAX_EVENTS];
    
    while (1) {
        int num_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_fds == -1) {
            die("epoll_wait()");
        }
         
        for (int i = 0; i < num_fds; i++) {
            int fd = events[i].data.fd;
            
            if (fd == listen_fd) {
                handle_new_connection(listen_fd, epoll_fd);
            } else { 
                handle_client_data(fd, epoll_fd);
            }
        }
    }
    
    close(epoll_fd);
    close(listen_fd);
    return 0;
}
