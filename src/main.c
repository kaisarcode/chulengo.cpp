/**
 * kc-app - Ecosystem Blueprint Application
 * Summary: Reference implementation for explicit CLI argument parsing.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU GPL v3.0
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pal.h"

#define KC_APP_DEFAULT_NAME "World"

/**
 * Displays application usage help.
 * @return void
 */
void kc_app_help(void) {
    printf("Options:\n");
    printf("  --name <name>      Name to greet\n");
    printf("  --fd-in <n>        Input descriptor for optional name read\n");
    printf("  --fd-out <n>       Output descriptor for greeting\n");
    printf("  --help             Show help\n");
}

/**
 * Prints an error message to standard error.
 * @param bin Name of the binary.
 * @param message Error message.
 * @return int Always returns 1.
 */
int kc_app_fail(const char *bin, const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    (void)bin;
    kc_app_help();
    return 1;
}

/**
 * Parses one positive file descriptor value.
 * @param text Raw descriptor text.
 * @param out Parsed descriptor output.
 * @return int 0 on success, 1 on failure.
 */
static int kc_app_parse_fd(const char *text, int *out) {
    char *end = NULL;
    long value;

    if (!text || !text[0] || !out) {
        return 1;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < 0 || value > INT_MAX) {
        return 1;
    }
    *out = (int)value;
    return 0;
}

/**
 * Writes one complete text buffer to the selected output descriptor.
 * @param fd Output descriptor.
 * @param text Text to write.
 * @return int 0 on success, 1 on failure.
 */
static int kc_app_write_text(int fd, const char *text) {
    size_t length = 0;

    if (!text) {
        return 1;
    }
    length = strlen(text);
    while (length > 0) {
        int written = KC_APP_WRITE(fd, text, (unsigned int)length);
        if (written < 0) {
            return 1;
        }
        if (written == 0) {
            return 1;
        }
        text += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

/**
 * Trims trailing end-of-line bytes from one mutable string.
 * @param text Mutable string buffer.
 * @return void
 */
static void kc_app_trim_eol(char *text) {
    size_t length = 0;

    if (!text) {
        return;
    }
    length = strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

/**
 * Reads one optional name string from the selected input descriptor.
 * @param fd Input descriptor.
 * @param out Output text pointer.
 * @return int 0 on success, 1 on failure.
 */
static int kc_app_read_name(int fd, char **out) {
    char chunk[256];
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;

    if (!out) {
        return 1;
    }

    for (;;) {
        int count = KC_APP_READ(fd, chunk, sizeof(chunk));
        if (count < 0) {
            free(buffer);
            return 1;
        }
        if (count == 0) {
            break;
        }
        if (used + (size_t)count + 1 > capacity) {
            char *next;
            capacity = capacity == 0 ? 256u : capacity * 2u;
            while (capacity < used + (size_t)count + 1) {
                capacity *= 2u;
            }
            next = realloc(buffer, capacity);
            if (!next) {
                free(buffer);
                return 1;
            }
            buffer = next;
        }
        memcpy(buffer + used, chunk, (size_t)count);
        used += (size_t)count;
        buffer[used] = '\0';
        if (memchr(chunk, '\n', (size_t)count) || memchr(chunk, '\r', (size_t)count)) {
            break;
        }
    }

    if (!buffer) {
        buffer = malloc(1u);
        if (!buffer) {
            return 1;
        }
        buffer[0] = '\0';
    }

    kc_app_trim_eol(buffer);
    *out = buffer;
    return 0;
}

/**
 * Application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int Process status code.
 */
int main(int argc, char **argv) {
    const char *name = KC_APP_DEFAULT_NAME;
    const char *arg_name = NULL;
    int input_fd = KC_APP_STDIN_FD;
    int output_fd = KC_APP_STDOUT_FD;
    char *input_name = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            kc_app_help();
            return 0;
        }
        if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 >= argc) {
                return kc_app_fail(argv[0], "Missing value for --name.");
            }
            arg_name = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--fd-in") == 0) {
            if (i + 1 >= argc) {
                return kc_app_fail(argv[0], "Missing value for --fd-in.");
            }
            if (kc_app_parse_fd(argv[++i], &input_fd) != 0) {
                return kc_app_fail(argv[0], "Invalid value for --fd-in.");
            }
            continue;
        }
        if (strcmp(argv[i], "--fd-out") == 0) {
            if (i + 1 >= argc) {
                return kc_app_fail(argv[0], "Missing value for --fd-out.");
            }
            if (kc_app_parse_fd(argv[++i], &output_fd) != 0) {
                return kc_app_fail(argv[0], "Invalid value for --fd-out.");
            }
            continue;
        }
        return kc_app_fail(argv[0], "Unknown argument.");
    }

    if (kc_app_read_name(input_fd, &input_name) != 0) {
        free(input_name);
        return kc_app_fail(argv[0], "Unable to read input content.");
    }

    if (input_name[0] != '\0') {
        name = input_name;
    } else if (arg_name && arg_name[0] != '\0') {
        name = arg_name;
    }

    {
        char output[4096];
        if (snprintf(output, sizeof(output), "Hello, %s!\n", name) >= (int)sizeof(output)) {
            free(input_name);
            return kc_app_fail(argv[0], "Greeting output is too large.");
        }
        if (kc_app_write_text(output_fd, output) != 0) {
            free(input_name);
            return kc_app_fail(argv[0], "Unable to write greeting.");
        }
    }

    free(input_name);
    return 0;
}
