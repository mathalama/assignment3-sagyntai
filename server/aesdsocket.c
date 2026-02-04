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

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BACKLOG 10
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;
volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        keep_running = 0;
        // Shutdown sockets to break out of blocking calls if possible
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

int main(int argc, char *argv[]) {
    bool is_daemon = false;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        is_daemon = true;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Setup signal handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

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

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (keep_running) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (keep_running) { 
                perror("accept");
            }
            continue; // Will exit loop if keep_running is false
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Receive data
        char *buffer = NULL;
        size_t current_len = 0;
        char temp_buffer[BUFFER_SIZE];
        ssize_t bytes_received;

        bool packet_complete = false;
        while (!packet_complete && keep_running) {
            bytes_received = recv(client_fd, temp_buffer, BUFFER_SIZE, 0);
            if (bytes_received <= 0) {
                break; // Connection closed or error
            }

            char *new_buffer = realloc(buffer, current_len + bytes_received + 1);
            if (!new_buffer) {
                perror("realloc");
                free(buffer);
                buffer = NULL;
                break;
            }
            buffer = new_buffer;
            
            memcpy(buffer + current_len, temp_buffer, bytes_received);
            current_len += bytes_received;
            buffer[current_len] = '\0';

            if (memchr(temp_buffer, '\n', bytes_received)) {
                packet_complete = true;
            }
        }

        if (packet_complete && buffer) {
            // Write to file
            FILE *fp = fopen(DATA_FILE, "a");
            if (fp) {
                fwrite(buffer, 1, current_len, fp);
                fclose(fp);
            } else {
                perror("fopen write");
            }

            // Send full file content back
            fp = fopen(DATA_FILE, "r");
            if (fp) {
                char send_buffer[BUFFER_SIZE];
                size_t bytes_read;
                while ((bytes_read = fread(send_buffer, 1, BUFFER_SIZE, fp)) > 0) {
                    send(client_fd, send_buffer, bytes_read, 0);
                }
                fclose(fp);
            } else {
                perror("fopen read");
            }
        }

        if (buffer) free(buffer);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        client_fd = -1;
    }

    if (server_fd != -1) {
        close(server_fd);
    }
    // Only remove file on exit if we are catching signals, 
    // but the requirement says "delete file /var/tmp/aesdsocketdata"
    remove(DATA_FILE);
    closelog();
    return 0;
}