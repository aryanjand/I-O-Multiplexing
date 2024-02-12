#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "file.h"

#define MAX_ASCII_CHAR 256

typedef struct
{
    unsigned long long word_count;
    unsigned long long character_count;
    unsigned long long character_frequency[256];
} TextStatistics;

typedef struct
{
    int socket_fd;         // Client's socket file descriptor
    TextStatistics *stats; // Statistics for this client
} ClientData;

// static void update_character_frequency(const char *word, uint8_t word_size, unsigned int *frequencyArray);
// static void read_stats(FILE *file, int sockfd);
// static void initialize_stats_zero(TextStatistics *stats);
// static void print_stats(TextStatistics *stats);
// static void handleError(const char *errorMessage, FILE *file, int sockfd, TextStatistics *stats, int exitProgram);

static void print_stats(TextStatistics *stats)
{
    printf("Word Count: %llu\n", stats->word_count);
    printf("Character Count: %llu\n", stats->character_count);
    printf("Character Frequency\n");
    for (int i = 0; i < MAX_ASCII_CHAR; i++)
    {
        if (stats->character_frequency[i] != 0)
            printf("Character: %c Frequency: %llu\n", (char)i, stats->character_frequency[i]);
    }
}

static void handleError(const char *errorMessage, int sockfd, TextStatistics *stats, int exitProgram)
{
    perror(errorMessage);

    if (stats)
    {
        free(stats);
    }

    if (sockfd >= 0)
    {
        close(sockfd);
    }

    if (exitProgram)
    {
        return;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static void update_character_frequency(const char *word, uint8_t word_size, unsigned int *frequencyArray) //[-Wunused-function]
{
    if (word == NULL || frequencyArray == NULL)
    {
        printf("Null pointer provided.\n");
        return;
    }

    for (int i = 0; i < word_size; i++)
    {
        if (word[i] == '\0')
        {
            printf("Early null terminator found at position %d.\n", i);
            break;
        }

        unsigned char lowered = (unsigned char)tolower(word[i]);
        frequencyArray[(int)lowered]++;
        // printf("Character: %c Frequency: %u\n", lowered, frequencyArray[(int) lowered]);
    }
}

static void read_stats(int sockfd) // [-Wunused-function]
{
    TextStatistics *stats = (TextStatistics *)calloc(1, sizeof(TextStatistics));
    size_t stats_len = sizeof(TextStatistics);

    if (!stats)
        handleError((char *)"Failed to allocate memory for stats", sockfd, stats, 1);

    ssize_t read_bytes;

    if ((read_bytes = read_fully(sockfd, &stats_len, sizeof(stats_len))) <= 0)
        handleError("Failed to read stats length", sockfd, stats, 1);

    stats = realloc(stats, stats_len);
    if (!stats)
        handleError("Failed to reallocate memory for stats", sockfd, NULL, 1);

    if ((read_bytes = read_fully(sockfd, stats, stats_len)) <= 0)
        handleError("Failed to read stats data", sockfd, stats, 1);

    printf("Bytes read %zu\n", read_bytes);

    print_stats(stats);
    free(stats);
}

static void initialize_stats_zero(TextStatistics *stats) // [-Wunused-function]
{
    stats->word_count = 0;
    stats->character_count = 0;
    memset(stats->character_frequency, 0, MAX_ASCII_CHAR * sizeof(unsigned int));
}

#pragma GCC diagnostic pop