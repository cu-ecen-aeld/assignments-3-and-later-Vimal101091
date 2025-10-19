#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#define PORT 9000
#define DATAFILE "/var/tmp/aesdsocketdata"

static int sockfd = -1;
static int clientfd = -1;
static volatile sig_atomic_t stop = 0;

void cleanup() {
    if (clientfd != -1) close(clientfd);
    if (sockfd != -1) close(sockfd);
    unlink(DATAFILE);
    closelog();
}

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        stop = 1;
    }
}

int daemonize() {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(0);   // parent exits

    if (setsid() < 0) return -1;

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    return 0;
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    struct sockaddr_in serv_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char buffer[1024];
    ssize_t bytes;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    if (listen(sockfd, 5) < 0) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    if (daemon_mode) {
        if (daemonize() != 0) {
            syslog(LOG_ERR, "daemonize failed");
            cleanup();
            return -1;
        }
    }
    
     // Register signal handler for SIGINT and SIGTERM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction failed for SIGINT");
        return 1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction failed for SIGTERM");
        return 1;
    }

    while (!stop) {
        clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
        if (clientfd < 0) {
            if (stop) break;
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        int fd = open(DATAFILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            syslog(LOG_ERR, "Failed to open %s: %s", DATAFILE, strerror(errno));
            close(clientfd);
            continue;
        }

        char *packet_buf = NULL;
        size_t packet_size = 0;

        while ((bytes = recv(clientfd, buffer, sizeof(buffer), 0)) > 0) {
            packet_buf = realloc(packet_buf, packet_size + bytes + 1);
            if (!packet_buf) {
                syslog(LOG_ERR, "malloc/realloc failed");
                break;
            }
            memcpy(packet_buf + packet_size, buffer, bytes);
            packet_size += bytes;
            packet_buf[packet_size] = '\0';

            char *newline_pos;
            while ((newline_pos = strchr(packet_buf, '\n')) != NULL) {
                size_t line_len = newline_pos - packet_buf + 1;

                if (write(fd, packet_buf, line_len) != line_len) {
                    syslog(LOG_ERR, "write failed: %s", strerror(errno));
                }
                fsync(fd);

                int filefd = open(DATAFILE, O_RDONLY);
                if (filefd != -1) {
                    char sendbuf[1024];
                    ssize_t n;
                    while ((n = read(filefd, sendbuf, sizeof(sendbuf))) > 0) {
                        send(clientfd, sendbuf, n, 0);
                    }
                    close(filefd);
                }

                size_t remaining = packet_size - line_len;
                memmove(packet_buf, packet_buf + line_len, remaining);
                packet_size = remaining;
                packet_buf[packet_size] = '\0';
            }
        }

        free(packet_buf);
        close(fd);
        close(clientfd);
        clientfd = -1;

        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
    }

    cleanup();
    return 0;
}

