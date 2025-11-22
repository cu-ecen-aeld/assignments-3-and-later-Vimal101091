
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define PORT 9000
#define DATAFILE "/var/tmp/aesdsocketdata"
#define BACKLOG 5
#define BUF_SIZE 1024
#define TIMESTAMP_INTERVAL 10

/* ---------- Globals ---------- */
static volatile sig_atomic_t exit_requested = 0;
static int listen_fd = -1;

/* Mutexes */
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Thread node for SLIST */
struct thread_node {
    pthread_t tid;
    int conn_fd;
    int completed;
    SLIST_ENTRY(thread_node) entries;
};
SLIST_HEAD(thread_list, thread_node);
static struct thread_list threads = SLIST_HEAD_INITIALIZER(threads);

/* ---------- Forward declarations (modular functions) ---------- */

/* setup / utility */
static void setup_signals(void);
static int setup_listen_socket(void);
static void daemonize_process(void);

/* list management */
static struct thread_node *create_thread_node(int conn_fd);
static void insert_thread_node(struct thread_node *node);
static struct thread_node *pop_completed_node(void);
static void reap_completed_nodes(void);

/* file operations */
static int write_line_to_file(const char *buf, size_t len);
static int send_file_to_client(int client_fd);

/* worker & timer */
static void *worker_fn(void *arg);
static void *timer_fn(void *arg);
static int start_timer_thread(pthread_t *tid);

/* shutdown helpers */
static void shutdown_all_clients(void);

/* ---------- Implementations ---------- */

/* Signal handler: request exit and close the listening socket (to break accept) */
static void handle_signal(int sig)
{
    (void)sig;
    exit_requested = 1;
    if (listen_fd != -1) {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
        listen_fd = -1;
    }
}

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* Create, bind, listen socket (blocking accept) */
static int setup_listen_socket(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        syslog(LOG_ERR, "socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "setsockopt() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(PORT);

    if (bind(fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        syslog(LOG_ERR, "bind() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* Simple daemonize (double fork). Call only when no worker/timer threads exist. */
static void daemonize_process(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork() failed: %s", strerror(errno));
        return;
    }
    if (pid > 0) {
        /* parent exits */
        exit(EXIT_SUCCESS);
    }

    /* child */
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid() failed: %s", strerror(errno));
    }

    pid_t pid2 = fork();
    if (pid2 < 0) {
        syslog(LOG_ERR, "second fork() failed: %s", strerror(errno));
        return;
    }
    if (pid2 > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") < 0) {
        syslog(LOG_WARNING, "chdir(/) failed: %s", strerror(errno));
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

/* Create a thread_node for a connection */
static struct thread_node *create_thread_node(int conn_fd)
{
    struct thread_node *node = calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->conn_fd = conn_fd;
    node->completed = 0;
    return node;
}

/* Insert node into SLIST (head insertion) */
static void insert_thread_node(struct thread_node *node)
{
    pthread_mutex_lock(&list_mutex);
    SLIST_INSERT_HEAD(&threads, node, entries);
    pthread_mutex_unlock(&list_mutex);
}

/* Remove and return the first completed node; returns NULL if none */
static struct thread_node *pop_completed_node(void)
{
    pthread_mutex_lock(&list_mutex);
    struct thread_node *it;
    SLIST_FOREACH(it, &threads, entries) {
        if (it->completed) {
            SLIST_REMOVE(&threads, it, thread_node, entries);
            pthread_mutex_unlock(&list_mutex);
            return it;
        }
    }
    pthread_mutex_unlock(&list_mutex);
    return NULL;
}

/* Join and free all currently completed nodes (opportunistic) */
static void reap_completed_nodes(void)
{
    while (1) {
        struct thread_node *done = pop_completed_node();
        if (!done) break;
        if (pthread_join(done->tid, NULL) != 0) {
            syslog(LOG_ERR, "pthread_join failed");
        }
        free(done);
    }
}

/* Append 'len' bytes from buf to DATAFILE atomically using file_mutex */
static int write_line_to_file(const char *buf, size_t len)
{
    int rc = -1;
    if (pthread_mutex_lock(&file_mutex) != 0) {
        syslog(LOG_ERR, "file_mutex lock failed");
        return -1;
    }

    int fd = open(DATAFILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "open() append failed: %s", strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        return -1;
    }

    ssize_t w = write(fd, buf, len);
    if (w != (ssize_t)len) {
        syslog(LOG_ERR, "write() failed: %s", strerror(errno));
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        return -1;
    }
    fsync(fd);
    close(fd);
    rc = 0;

    pthread_mutex_unlock(&file_mutex);
    return rc;
}

/* Send entire DATAFILE contents to client_fd; caller should hold no mutex */
static int send_file_to_client(int client_fd)
{
    if (pthread_mutex_lock(&file_mutex) != 0) {
        syslog(LOG_ERR, "file_mutex lock failed (send_file)");
        return -1;
    }

    int fd = open(DATAFILE, O_RDONLY);
    if (fd < 0) {
        /* If file doesn't exist, treat as empty */
        if (errno == ENOENT) {
            pthread_mutex_unlock(&file_mutex);
            return 0;
        }
        syslog(LOG_ERR, "open() read failed: %s", strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        return -1;
    }

    char buf[BUF_SIZE];
    ssize_t nr;
    while ((nr = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t sent_total = 0;
        while (sent_total < nr) {
            ssize_t s = send(client_fd, buf + sent_total, (size_t)(nr - sent_total), 0);
            if (s < 0) {
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "send() failed: %s", strerror(errno));
                close(fd);
                pthread_mutex_unlock(&file_mutex);
                return -1;
            }
            sent_total += s;
        }
    }
    close(fd);
    pthread_mutex_unlock(&file_mutex);
    return 0;
}

/* Worker thread: read until newline(s). For each newline-terminated packet:
   - append to datafile (atomic)
   - send entire file back to client
   On exit, mark node->completed (under list_mutex) and close local fd. */
static void *worker_fn(void *arg)
{
    struct thread_node *node = (struct thread_node *)arg;
    int local_fd;

    /* take local copy of fd */
    pthread_mutex_lock(&list_mutex);
    local_fd = node->conn_fd;
    pthread_mutex_unlock(&list_mutex);

    char inbuf[BUF_SIZE];
    char *acc = NULL;
    size_t acc_len = 0;

    while (!exit_requested) {
        ssize_t r = recv(local_fd, inbuf, sizeof(inbuf), 0);
        if (r == 0) break; /* client closed */
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }

        char *tmp = realloc(acc, acc_len + (size_t)r + 1);
        if (!tmp) {
            syslog(LOG_ERR, "realloc failed");
            free(acc);
            acc = NULL;
            acc_len = 0;
            break;
        }
        acc = tmp;
        memcpy(acc + acc_len, inbuf, (size_t)r);
        acc_len += (size_t)r;
        acc[acc_len] = '\0';

        /* process newline-terminated lines */
        char *nl;
        while ((nl = memchr(acc, '\n', acc_len)) != NULL) {
            size_t line_len = (size_t)(nl - acc) + 1;

            /* append line atomically */
            if (write_line_to_file(acc, line_len) != 0) {
                /* error already logged */
                goto worker_cleanup;
            }

            /* send full file back to client */
            if (send_file_to_client(local_fd) != 0) {
                goto worker_cleanup;
            }

            /* remove processed part */
            size_t remaining = acc_len - line_len;
            if (remaining > 0) memmove(acc, acc + line_len, remaining);
            acc_len = remaining;
            if (acc_len == 0) {
                free(acc);
                acc = NULL;
            } else {
                char *shr = realloc(acc, acc_len + 1);
                if (shr) acc = shr;
                acc[acc_len] = '\0';
            }
        }
    }

worker_cleanup:
    free(acc);

    /* mark completed under lock */
    pthread_mutex_lock(&list_mutex);
    node->completed = 1;
    pthread_mutex_unlock(&list_mutex);

    if (local_fd != -1) close(local_fd);
    return NULL;
}

/* Timer thread: append RFC2822 timestamp every TIMESTAMP_INTERVAL seconds */
static void *timer_fn(void *arg)
{
    (void)arg;
    while (!exit_requested) {
        for (int i = 0; i < TIMESTAMP_INTERVAL && !exit_requested; ++i) {
            struct timespec ts = {1, 0};
            nanosleep(&ts, NULL);
        }
        if (exit_requested) break;

        time_t now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        char timestr[128];
        if (strftime(timestr, sizeof(timestr), "%a, %d %b %Y %H:%M:%S %z", &tm_buf) == 0) {
            snprintf(timestr, sizeof(timestr), "%ld", (long)now);
        }
        char out[256];
        int n = snprintf(out, sizeof(out), "timestamp:%s\n", timestr);
        if (n > 0) {
            write_line_to_file(out, (size_t)n);
        }
    }
    return NULL;
}

/* Start timer thread, return 0 on success */
static int start_timer_thread(pthread_t *tid)
{
    if (pthread_create(tid, NULL, timer_fn, NULL) != 0) {
        syslog(LOG_ERR, "pthread_create(timer) failed");
        return -1;
    }
    return 0;
}

/* Shutdown client sockets by calling shutdown() on each; workers will close fds */
static void shutdown_all_clients(void)
{
    pthread_mutex_lock(&list_mutex);
    struct thread_node *it;
    SLIST_FOREACH(it, &threads, entries) {
        if (it->conn_fd != -1) {
            shutdown(it->conn_fd, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&list_mutex);
}

/* ---------- Main ---------- */
int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) daemon_mode = 1;

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    SLIST_INIT(&threads);

    /* 1) Remove previous data file so tests start fresh (per choice 1) */
    (void)unlink(DATAFILE); /* ignore error */

    /* 2) Setup signals */
    setup_signals();

    /* 3) Setup listen socket */
    listen_fd = setup_listen_socket();
    if (listen_fd < 0) {
        closelog();
        return EXIT_FAILURE;
    }

    /* 4) Ensure data file exists (fresh) */
    int tmpfd = open(DATAFILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (tmpfd < 0) {
        syslog(LOG_ERR, "unable to create %s: %s", DATAFILE, strerror(errno));
        close(listen_fd);
        closelog();
        return EXIT_FAILURE;
    }
    close(tmpfd);

    /* 5) Daemonize if requested (do this before starting timer/worker threads) */
    if (daemon_mode) daemonize_process();

    /* 6) Start timer thread (after daemonize to avoid fork-with-threads issues) */
    pthread_t timer_tid;
    if (start_timer_thread(&timer_tid) != 0) {
        close(listen_fd);
        closelog();
        return EXIT_FAILURE;
    }

    /* 7) Accept loop (blocking accept) */
    while (!exit_requested) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int cfd = accept(listen_fd, (struct sockaddr *)&client, &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (exit_requested) break;
            syslog(LOG_ERR, "accept() failed: %s", strerror(errno));
            continue;
        }

        /* create node and add to list */
        struct thread_node *node = create_thread_node(cfd);
        if (!node) {
            syslog(LOG_ERR, "calloc failed for thread_node");
            close(cfd);
            continue;
        }
        insert_thread_node(node);

        /* start worker thread */
        if (pthread_create(&node->tid, NULL, worker_fn, node) != 0) {
            syslog(LOG_ERR, "pthread_create(worker) failed");
            /* cleanup node */
            pthread_mutex_lock(&list_mutex);
            SLIST_REMOVE(&threads, node, thread_node, entries);
            pthread_mutex_unlock(&list_mutex);
            close(cfd);
            free(node);
            continue;
        }

        /* Reap completed worker threads opportunistically */
        reap_completed_nodes();
    }

    /* Begin shutdown */
    exit_requested = 1;

    if (listen_fd != -1) {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
        listen_fd = -1;
    }

    /* Cause workers to unblock from recv() */
    shutdown_all_clients();

    /* Join timer thread */
    if (pthread_join(timer_tid, NULL) != 0) {
        syslog(LOG_ERR, "pthread_join(timer) failed");
    }

    /* Join & free remaining worker nodes */
    while (1) {
        struct thread_node *done = pop_completed_node();
        if (!done) {
            pthread_mutex_lock(&list_mutex);
            int empty = SLIST_EMPTY(&threads);
            pthread_mutex_unlock(&list_mutex);
            if (empty) break;
            struct timespec ts = {0, 10000000}; /* 10ms */
            nanosleep(&ts, NULL);
            continue;
        }
        if (pthread_join(done->tid, NULL) != 0) {
            syslog(LOG_ERR, "pthread_join failed");
        }
        free(done);
    }

    /* Don't unlink(DATAFILE) here; tests expect content to remain */

    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&list_mutex);

    syslog(LOG_INFO, "aesdsocket exiting");
    closelog();
    return EXIT_SUCCESS;
}

