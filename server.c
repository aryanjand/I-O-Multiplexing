/*
 * This code is licensed under the Attribution-NonCommercial-NoDerivatives 4.0 International license.
 *
 * Author: D'Arcy Smith (ds@programming101.dev)
 *
 * You are free to:
 *   - Share: Copy and redistribute the material in any medium or format.
 *   - Under the following terms:
 *       - Attribution: You must give appropriate credit, provide a link to the license, and indicate if changes were made.
 *       - NonCommercial: You may not use the material for commercial purposes.
 *       - NoDerivatives: If you remix, transform, or build upon the material, you may not distribute the modified material.
 *
 * For more details, please refer to the full license text at:
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <sys/un.h>

#include "text_statistics.h"

static void setup_signal_handler(void);
static void sigint_handler(int signum);
static void parse_arguments(int argc, char *argv[], char **ip_address, char **port, char **backlog);
static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, const char *backlog_str, in_port_t *port, int *backlog);
static in_port_t parse_in_port_t(const char *binary_name, const char *port_str);
static int parse_positive_int(const char *binary_name, const char *str);
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);
static void convert_address(const char *address, struct sockaddr_storage *addr);
static int socket_create(int domain, int type, int protocol);
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void start_listening(int server_fd, int backlog);
static void socket_close(int sockfd);

// Polling
static struct pollfd *initialize_pollfds(int sockfd, ClientData **client_sockets);
static void handle_new_connection(int sockfd, ClientData **client_sockets, nfds_t *max_clients, struct pollfd **fds, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);
static void handle_client_data(struct pollfd *fds, ClientData *client_sockets, nfds_t *max_clients);
static void handle_client_disconnection(ClientData **client_sockets, nfds_t *max_clients, struct pollfd **fds, nfds_t client_index);

#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
#define MAX_WORD_LEN 256

static volatile sig_atomic_t exit_flag = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int main(int argc, char *argv[])
{
    char *address;
    char *port_str;
    char *backlog_str;
    in_port_t port;
    int backlog;
    struct sockaddr_storage addr;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;

    // Polling
    int sockfd;
    ClientData *client_sockets = NULL;
    nfds_t max_clients = 0;
    struct pollfd *fds;

    // Setup the server
    address = NULL;
    port_str = NULL;
    backlog_str = NULL;
    parse_arguments(argc, argv, &address, &port_str, &backlog_str);
    handle_arguments(argv[0], address, port_str, backlog_str, &port, &backlog);
    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_bind(sockfd, &addr, port);
    start_listening(sockfd, backlog);
    setup_signal_handler();

    fds = initialize_pollfds(sockfd, &client_sockets);
    while (!exit_flag)
    {
        int activity;

        activity = poll(fds, max_clients + 1, -1);

        if (activity < 0)
        {
            perror("Poll error");
            exit(EXIT_FAILURE);
        }
        // printf("Polling Started\n");
        // Handle new client connections
        client_addr_len = sizeof(client_addr);
        handle_new_connection(sockfd, &client_sockets, &max_clients, &fds, &client_addr, &client_addr_len);
        // printf("Connection Made\n");

        if (client_sockets != NULL)
        {
            // Handle incoming data from existing clients
            // printf("Handling Client Data\n");
            handle_client_data(fds, client_sockets, &max_clients);
        }
    }

    free(fds);

    // Cleanup and close all client sockets
    for (size_t i = 0; i < max_clients; i++)
    {
        if (client_sockets[i].socket_fd > 0)
        {
            socket_close(client_sockets[i].socket_fd);
        }
    }

    free(client_sockets);
    socket_close(sockfd);
    printf("Server exited successfully.\n");

    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port, char **backlog)
{
    int opt;

    opterr = 0;

    while ((opt = getopt(argc, argv, "hb:")) != -1)
    {
        switch (opt)
        {
        case 'b':
        {
            *backlog = optarg;
            break;
        }
        case 'h':
        {
            usage(argv[0], EXIT_SUCCESS, NULL);
        }
        case '?':
        {
            char message[UNKNOWN_OPTION_MESSAGE_LEN];

            snprintf(message, sizeof(message), "Unknown option '-%c'.", optopt);
            usage(argv[0], EXIT_FAILURE, message);
        }
        default:
        {
            usage(argv[0], EXIT_FAILURE, NULL);
        }
        }
    }

    if (optind >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The ip address and port are required");
    }

    if (optind + 1 >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The port is required");
    }

    if (optind < argc - 2)
    {
        usage(argv[0], EXIT_FAILURE, "Error: Too many arguments.");
    }

    *ip_address = argv[optind];
    *port = argv[optind + 1];
}

static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, const char *backlog_str, in_port_t *port, int *backlog)
{
    if (ip_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The ip address is required.");
    }

    if (port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    if (backlog_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The backlog is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
    *backlog = parse_positive_int(binary_name, backlog_str);
}

in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char *endptr;
    uintmax_t parsed_value;

    errno = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if (errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if (*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within the valid range for in_port_t
    if (parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

int parse_positive_int(const char *binary_name, const char *str)
{
    char *endptr;
    intmax_t parsed_value;

    errno = 0;
    parsed_value = strtoimax(str, &endptr, BASE_TEN);

    if (errno != 0)
    {
        usage(binary_name, EXIT_FAILURE, "Error parsing integer.");
    }

    // Check if there are any non-numeric characters in the input string
    if (*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is non-negative
    if (parsed_value < 0 || parsed_value > INT_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "Integer out of range or negative.");
    }

    return (int)parsed_value;
}

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if (message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] -b <backlog> <ip address> <port>\n", program_name);
    fputs("Options:\n", stderr);
    fputs("  -h  Display this help message\n", stderr);
    fputs("  -b <backlog> the backlog\n", stderr);
    exit(exit_code);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void sigint_handler(int signum)
{
    exit_flag = 1;
}

#pragma GCC diagnostic pop

static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if (inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if (inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if (sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void *vaddr;
    in_port_t net_port;

    net_port = htons(port);

    if (addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr = (struct sockaddr_in *)addr;
        addr_len = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if (addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr = (struct sockaddr_in6 *)addr;
        addr_len = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr, "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if (inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to: %s:%u\n", addr_str, port);

    if (bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

static void start_listening(int server_fd, int backlog)
{
    if (listen(server_fd, backlog) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

static void setup_signal_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sigint_handler;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void socket_close(int sockfd)
{
    if (close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

static void handle_new_connection(int sockfd, ClientData **client_sockets, nfds_t *max_clients, struct pollfd **fds, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    if ((*fds)[0].revents & POLLIN)
    {
        ClientData *temp;
        TextStatistics *stats_temp;
        int new_socket;

        // printf("Accept request about to be made\n");
        new_socket = accept(sockfd, (struct sockaddr *)client_addr, client_addr_len);
        // printf("Accept request made, Socket: %d\n", new_socket);

        if (new_socket == -1)
        {
            perror("Accept error");
            exit(EXIT_FAILURE);
        }

        // printf("Allocating memory Client Data \n");
        (*max_clients)++;
        temp = (ClientData *)realloc(*client_sockets, sizeof(ClientData) * (*max_clients));
        if (temp == NULL)
        {
            perror("realloc");
            free(*client_sockets);
            exit(EXIT_FAILURE);
        }

        // printf("Allocating memory Text Statistics\n");
        stats_temp = (TextStatistics *)calloc(1, sizeof(TextStatistics));
        if (stats_temp == NULL)
        {
            perror("calloc");
            free(*client_sockets);
            free(temp);
            exit(EXIT_FAILURE);
        }
        initialize_stats_zero(stats_temp);
        // printf("Text Statistics to 0\n");
        struct pollfd *new_fds;
        *client_sockets = temp;
        (*client_sockets)[(*max_clients) - 1].socket_fd = new_socket;
        (*client_sockets)[(*max_clients) - 1].stats = stats_temp;

        // printf("Allocating memory new fds\n");
        new_fds = (struct pollfd *)realloc(*fds, (*max_clients + 1) * sizeof(struct pollfd));
        if (new_fds == NULL)
        {
            perror("realloc");
            free(*client_sockets);
            exit(EXIT_FAILURE);
        }
        *fds = new_fds;
        (*fds)[*max_clients].fd = new_socket;
        (*fds)[*max_clients].events = POLLIN;
    }
    // printf("End\n");
}

static void handle_client_data(struct pollfd *fds, ClientData *client_sockets, nfds_t *max_clients)
{
    for (nfds_t i = 0; i < *max_clients; i++)
    {
        if (client_sockets[i].socket_fd != -1 && (fds[i + 1].revents & POLLIN))
        {
            char word_length;
            ssize_t valread;

            valread = read(client_sockets[i].socket_fd, &word_length, sizeof(word_length));

            if (valread <= 0)
            {
                // Connection closed or error
                printf("Client %d disconnected\n", client_sockets[i].socket_fd);
                handle_client_disconnection(&client_sockets, max_clients, &fds, i);
            }
            else
            {
                char word[MAX_WORD_LEN];

                valread = read(client_sockets[i].socket_fd, word, (size_t)word_length);

                if (valread <= 0)
                {
                    // Connection closed or error
                    printf("Client %d disconnected\n", client_sockets[i].socket_fd);
                    handle_client_disconnection(&client_sockets, max_clients, &fds, i);
                }
                else
                {
                    word[valread] = '\0';
                    client_sockets[i].stats->word_count++;
                    client_sockets[i].stats->character_count += strlen(word);
                    update_character_frequency(word, strlen(word), client_sockets[i].stats->character_frequency);

                    printf("Received word from client %d: %s\n", client_sockets[i].socket_fd, word);
                }
            }
        }
    }
}

static void handle_client_disconnection(ClientData **client_sockets, nfds_t *max_clients, struct pollfd **fds, nfds_t client_index)
{
    size_t stats_len = sizeof(TextStatistics);
    write_stats(client_sockets, client_index, stats_len);
    print_stats((*client_sockets)[client_index].stats);

    int disconnected_socket = (*client_sockets)[client_index].socket_fd;
    close(disconnected_socket);

    if ((*client_sockets)[client_index].stats != NULL)
    {
        free((*client_sockets)[client_index].stats);
        (*client_sockets)[client_index].stats = NULL;
    }

    for (nfds_t i = client_index; i < *max_clients - 1; i++)
    {
        (*client_sockets)[i] = (*client_sockets)[i + 1];
    }

    (*max_clients)--;

    for (nfds_t i = client_index + 1; i <= *max_clients; i++)
    {
        (*fds)[i] = (*fds)[i + 1];
    }
}

static struct pollfd *initialize_pollfds(int sockfd, ClientData **client_sockets)
{
    struct pollfd *fds;

    *client_sockets = NULL;

    fds = (struct pollfd *)malloc((1) * sizeof(struct pollfd));

    if (fds == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    fds[0].fd = sockfd;
    fds[0].events = POLLIN;

    return fds;
}