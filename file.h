#include <unistd.h> 
#include <errno.h>

ssize_t read_fully(int fd, void *buf, size_t count);
ssize_t write_fully(int fd, const void *buf, size_t count);

ssize_t read_fully(int fd, void *buf, size_t count) 
{
    size_t total = 0;
    ssize_t n;

    while (total < count) {
        n = read(fd, (char *)buf + total, count - total);

        if (n == -1) {
            if (errno == EINTR) {
                continue; // Retry if interrupted by a signal
            } else {
                return -1; // Return -1 to indicate an error
            }
        } else if (n == 0) {
            break; // Break if the end of the file is reached
        }

        total += n;
    }

    return total;
}

ssize_t write_fully(int fd, const void *buf, size_t count) 
{
    size_t total = 0;
    ssize_t n;

    while (total < count) {
        n = write(fd, (const char *)buf + total, count - total);

        if (n == -1) {
            if (errno == EINTR) {
                continue; // Retry if interrupted by a signal
            } else {
                return -1; // Return -1 to indicate an error
            }
        }

        total += n;
    }

    return total;
}
