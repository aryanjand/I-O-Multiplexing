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
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>
#include <time.h>

#include "text_statistics.h"

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port, char **file_path);
static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port, const char *file_path);
static in_port_t parse_in_port_t(const char *binary_name, const char *port_str);
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);
static void convert_address(const char *address, struct sockaddr_storage *addr);
static int socket_create(int domain, int type, int protocol);
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void socket_close(int sockfd);

// poll
static void send_word(int sockfd, const char *word, uint8_t length);
_Noreturn static void error_exit(const char *msg);
static int connect_to_server(const char *path);
static void setup_socket_address(struct sockaddr_un *addr, const char *path);

#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define LINE_LEN 1024
#define MILLISECONDS_IN_NANOSECONDS 1000000
#define MIN_DELAY_MILLISECONDS 500
#define MAX_ADDITIONAL_NANOSECONDS 1000000000

int main(int argc, char *argv[])
{
    char *address;
    char *port_str;
    in_port_t port;
    int sockfd;
    struct sockaddr_storage addr;
    char ch;
    ssize_t nread;
    char *file_path;
    FILE *file;
    char line[LINE_LEN];
    char *saveptr;

    address = NULL;
    port_str = NULL;
    file_path = NULL;

    parse_arguments(argc, argv, &address, &port_str, &file_path);
    handle_arguments(argv[0], address, port_str, &port, file_path);
    file = fopen(file_path, "re");

    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_connect(sockfd, &addr, port);

    while (fgets(line, sizeof(line), file) != NULL)
    {
        const char *word;

        word = strtok_r(line, " \t\n", &saveptr);

        while (word != NULL)
        {
            uint8_t size;
            size_t word_len;

            word_len = strlen(word);

            if (word_len > UINT8_MAX)
            {
                fprintf(stderr, "Word exceeds maximum length\n");
                fclose(file);
                close(sockfd);
                exit(EXIT_FAILURE);
            }

            size = (uint8_t)word_len;
            send_word(sockfd, word, size);
            word = strtok_r(NULL, " \t\n", &saveptr);
        }
    }

    fclose(file);
    shutdown(sockfd, SHUT_WR); // Shutdown the write.
    read_stats(sockfd);

    socket_close(sockfd);

    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port, char **file_path)
{
    int opt;

    opterr = 0;

    while ((opt = getopt(argc, argv, "h")) != -1)
    {
        switch (opt)
        {
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

    if (optind + 1 >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "Too few arguments.");
    }

    if (optind < argc - 3)
    {
        usage(argv[0], EXIT_FAILURE, "Too many arguments.");
    }

    *ip_address = argv[optind];
    *port = argv[optind + 1];
    *file_path = argv[optind + 2];
}

static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port, const char *file_path)
{
    if (ip_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The ip address is required.");
    }

    if (port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    if (file_path == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The file path is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

static in_port_t parse_in_port_t(const char *binary_name, const char *str)
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

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if (message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] <ip address> <port>\n", program_name);
    fputs("Options:\n", stderr);
    fputs("  -h  Display this help message\n", stderr);
    exit(exit_code);
}

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

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char addr_str[INET6_ADDRSTRLEN];
    in_port_t net_port;
    socklen_t addr_len;

    if (inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to: %s:%u\n", addr_str, port);
    net_port = htons(port);

    if (addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len = sizeof(struct sockaddr_in);
    }
    else if (addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        const char *msg;

        msg = strerror(errno);
        fprintf(stderr, "Error: connect (%d): %s\n", errno, msg);
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}

static void socket_close(int client_fd)
{
    if (close(client_fd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

static void send_word(int sockfd, const char *word, uint8_t length)
{
    ssize_t written_bytes;
    struct timespec delay;

    printf("Client: sending word of length %u: %s\n", length, word);
    written_bytes = send(sockfd, &length, sizeof(uint8_t), 0);

    if (written_bytes < 0)
    {
        error_exit("Error writing word length to socket");
    }

    if (length > 0)
    {
        written_bytes = send(sockfd, word, length, 0);

        if (written_bytes < 0)
        {
            error_exit("Error writing word to socket");
        }
    }

    // Add random delay between 500ms and 1500ms
    delay.tv_sec = 0;
    delay.tv_nsec = MIN_DELAY_MILLISECONDS * MILLISECONDS_IN_NANOSECONDS + (rand() % MAX_ADDITIONAL_NANOSECONDS);
    nanosleep(&delay, NULL);
}

_Noreturn static void error_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

// static int connect_to_server(const char *path)
// {
//     int sockfd;
//     struct sockaddr_un addr;

//     sockfd = socket_create();
//     setup_socket_address(&addr, path);

//     if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
//     {
//         perror("Connection failed");
//         close(sockfd);
//         exit(EXIT_FAILURE);
//     }

//     printf("Connected to %s\n", path);

//     return sockfd;
// }

static void setup_socket_address(struct sockaddr_un *addr, const char *path)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    strncpy(addr->sun_path, path, sizeof(addr->sun_path) - 1);
    addr->sun_path[sizeof(addr->sun_path) - 1] = '\0';
}