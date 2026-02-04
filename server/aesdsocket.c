#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>
#include <sys/ioctl.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 1024

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif

// Thread data structure
struct thread_data_s {
    pthread_t thread_id;
    int client_fd;
    bool thread_complete;
    SLIST_ENTRY(thread_data_s) entries;
};

// Global variables
int server_fd = -1;
volatile sig_atomic_t keep_running = 1;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
SLIST_HEAD(slisthead, thread_data_s) head;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        keep_running = 0;
        if (server_fd != -1) {
            shutdown(server_fd, SHUT_RDWR);
        }
    }
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Parent exits
    }

    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
}

#if !USE_AESD_CHAR_DEVICE
void* timer_thread_func(void* arg) {
    while (keep_running) {
        for(int i=0; i<10; i++) {
             if(!keep_running) break;
             sleep(1); 
        }
        if (!keep_running) break;

        time_t rawtime;
        struct tm *info;
        char buffer[100];

        time(&rawtime);
        info = localtime(&rawtime);

        strftime(buffer, sizeof(buffer), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", info);

        pthread_mutex_lock(&file_mutex);
        int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd != -1) {
            write(fd, buffer, strlen(buffer));
            close(fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}
#endif

void* client_thread_func(void* thread_param) {
    struct thread_data_s* data = (struct thread_data_s*)thread_param;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    char* full_packet = NULL;
    size_t packet_len = 0;

    while (keep_running) {
        bytes_received = recv(data->client_fd, buffer, BUFFER_SIZE, 0);
        if (bytes_received < 0) {
            if (keep_running) perror("recv");
            break;
        }
        if (bytes_received == 0) {
            break; // Connection closed
        }

        char* new_packet = realloc(full_packet, packet_len + bytes_received + 1);
        if (!new_packet) {
            perror("realloc");
            free(full_packet);
            full_packet = NULL;
            break;
        }
        full_packet = new_packet;
        memcpy(full_packet + packet_len, buffer, bytes_received);
        packet_len += bytes_received;
        full_packet[packet_len] = '\0';

        if (memchr(buffer, '\n', bytes_received)) {
            pthread_mutex_lock(&file_mutex);
            
            bool is_ioctl = false;
            const char *ioctl_str = "AESDCHAR_IOCSEEKTO:";
            if (strncmp(full_packet, ioctl_str, strlen(ioctl_str)) == 0) {
                unsigned int write_cmd, write_cmd_offset;
                if (sscanf(full_packet + strlen(ioctl_str), "%u,%u", &write_cmd, &write_cmd_offset) == 2) {
                    is_ioctl = true;
                    int fd = open(DATA_FILE, O_RDWR);
                    if (fd != -1) {
                        struct aesd_seekto seekto;
                        seekto.write_cmd = write_cmd;
                        seekto.write_cmd_offset = write_cmd_offset;
                        if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto) == 0) {
                            // After ioctl, read remainder of file and send
                            char send_buf[BUFFER_SIZE];
                            ssize_t read_bytes;
                            while ((read_bytes = read(fd, send_buf, BUFFER_SIZE)) > 0) {
                                send(data->client_fd, send_buf, read_bytes, 0);
                            }
                        } else {
                            syslog(LOG_ERR, "ioctl failed: %s", strerror(errno));
                        }
                        close(fd);
                    }
                }
            }

            if (!is_ioctl) {
                int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
                if (fd != -1) {
                    write(fd, full_packet, packet_len);
                    close(fd);
                }
                
                fd = open(DATA_FILE, O_RDONLY);
                if (fd != -1) {
                    char send_buf[BUFFER_SIZE];
                    ssize_t read_bytes;
                    while ((read_bytes = read(fd, send_buf, BUFFER_SIZE)) > 0) {
                        send(data->client_fd, send_buf, read_bytes, 0);
                    }
                    close(fd);
                }
            }
            
            pthread_mutex_unlock(&file_mutex);
            
            free(full_packet);
            full_packet = NULL;
            packet_len = 0;
        }
    }
    
    if (full_packet) free(full_packet);
    close(data->client_fd);
    data->thread_complete = true;
    return NULL;
}

int main(int argc, char *argv[]) {
    bool is_daemon = false;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        is_daemon = true;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    SLIST_INIT(&head);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (is_daemon) {
        daemonize();
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return -1;
    }

#if !USE_AESD_CHAR_DEVICE
    pthread_t timer_thread;
    if (pthread_create(&timer_thread, NULL, timer_thread_func, NULL) != 0) {
        perror("pthread_create timer");
    }
#endif

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (keep_running) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_fd == -1) {
            if (keep_running) {
                perror("accept");
            }
        } else {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            syslog(LOG_INFO, "Accepted connection from %s", client_ip);

            struct thread_data_s *new_thread_data = malloc(sizeof(struct thread_data_s));
            if (!new_thread_data) {
                perror("malloc");
                close(client_fd);
                continue;
            }
            new_thread_data->client_fd = client_fd;
            new_thread_data->thread_complete = false;

            if (pthread_create(&new_thread_data->thread_id, NULL, client_thread_func, new_thread_data) != 0) {
                perror("pthread_create");
                free(new_thread_data);
                close(client_fd);
                continue;
            }
            
            SLIST_INSERT_HEAD(&head, new_thread_data, entries);
        }
        
        struct thread_data_s *entry = NULL;
        struct thread_data_s **ptr = &SLIST_FIRST(&head);
        while ((entry = *ptr) != NULL) {
            if (entry->thread_complete) {
                pthread_join(entry->thread_id, NULL);
                *ptr = SLIST_NEXT(entry, entries);
                free(entry);
            } else {
                ptr = &SLIST_NEXT(entry, entries);
            }
        }
    }

#if !USE_AESD_CHAR_DEVICE
    pthread_join(timer_thread, NULL);
#endif

    while (!SLIST_EMPTY(&head)) {
        struct thread_data_s *entry = SLIST_FIRST(&head);
        SLIST_REMOVE_HEAD(&head, entries);
        shutdown(entry->client_fd, SHUT_RDWR);
        pthread_join(entry->thread_id, NULL);
        free(entry);
    }

    if (server_fd != -1) {
        close(server_fd);
    }
#if !USE_AESD_CHAR_DEVICE
    remove(DATA_FILE);
#endif
    closelog();
    return 0;
}
