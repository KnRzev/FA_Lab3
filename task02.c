#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MINUTES_PER_DAY = 1440 };

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_INVALID_FORMAT,
    STATUS_OUT_OF_RANGE,
    STATUS_DUPLICATE_ID,
    STATUS_ALLOCATION_ERROR,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT
} Status;

typedef struct {
    size_t id;
    unsigned int start_time;
    unsigned int finish_time;
} Talk;

static int compare_ids(const void *const left, const void *const right) {
    const Talk *const first = left;
    const Talk *const second = right;
    return (first->id > second->id) - (first->id < second->id);
}

static int compare_finish_times(const void *const left, const void *const right) {
    const Talk *const first = left;
    const Talk *const second = right;
    if (first->finish_time != second->finish_time) {
        return (first->finish_time > second->finish_time)
             - (first->finish_time < second->finish_time);
    }
    if (first->start_time != second->start_time) {
        return (first->start_time > second->start_time)
             - (first->start_time < second->start_time);
    }
    return compare_ids(left, right);
}

Status max_talk_count(const Talk *const talks, const size_t count,
                      const size_t hall_count, size_t *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    *result = 0U;
    if (talks == NULL || count == 0U || hall_count == 0U) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (count > SIZE_MAX / sizeof(Talk)) {
        return STATUS_OUT_OF_RANGE;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (talks[i].id == 0U || talks[i].start_time >= talks[i].finish_time
            || talks[i].finish_time >= MINUTES_PER_DAY) {
            return STATUS_INVALID_ARGUMENT;
        }
    }

    Talk *const sorted = malloc(count * sizeof(*sorted));
    if (sorted == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }
    memcpy(sorted, talks, count * sizeof(*sorted));
    qsort(sorted, count, sizeof(*sorted), compare_ids);
    for (size_t i = 1U; i < count; ++i) {
        if (sorted[i - 1U].id == sorted[i].id) {
            free(sorted);
            return STATUS_DUPLICATE_ID;
        }
    }
    qsort(sorted, count, sizeof(*sorted), compare_finish_times);

    size_t available[MINUTES_PER_DAY] = {0U};
    available[0] = hall_count < count ? hall_count : count;
    size_t selected = 0U;
    for (size_t i = 0U; i < count; ++i) {
        unsigned int minute = sorted[i].start_time;
        while (minute > 0U && available[minute] == 0U) {
            --minute;
        }
        if (available[minute] != 0U) {
            --available[minute];
            ++available[sorted[i].finish_time];
            ++selected;
        }
    }

    free(sorted);
    *result = selected;
    return STATUS_OK;
}

static Status read_line(FILE *const stream, char **const buffer,
                        size_t *const capacity) {
    if (stream == NULL || buffer == NULL || capacity == NULL
        || ((*buffer == NULL) != (*capacity == 0U))) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (*buffer == NULL) {
        *buffer = malloc(64U);
        if (*buffer == NULL) {
            return STATUS_ALLOCATION_ERROR;
        }
        *capacity = 64U;
    }

    size_t length = 0U;
    int character = 0;
    while ((character = fgetc(stream)) != EOF && character != '\n') {
        if (character == '\0') {
            return STATUS_INVALID_FORMAT;
        }
        if (length == *capacity - 1U) {
            if (*capacity > SIZE_MAX / 2U) {
                return STATUS_OUT_OF_RANGE;
            }
            const size_t new_capacity = *capacity * 2U;
            char *const resized = realloc(*buffer, new_capacity);
            if (resized == NULL) {
                return STATUS_ALLOCATION_ERROR;
            }
            *buffer = resized;
            *capacity = new_capacity;
        }
        (*buffer)[length++] = (char)character;
    }
    if (ferror(stream)) {
        return STATUS_IO_ERROR;
    }
    (*buffer)[length] = '\0';
    return character == EOF && length == 0U ? STATUS_END_OF_INPUT : STATUS_OK;
}

static Status split_line(char *const line, char **const tokens,
                         const size_t expected) {
    if (line == NULL || tokens == NULL || expected == 0U) {
        return STATUS_INVALID_ARGUMENT;
    }
    char *cursor = line;
    size_t count = 0U;
    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor != '\0') {
            if (count == expected) {
                return STATUS_INVALID_FORMAT;
            }
            tokens[count++] = cursor;
            while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (*cursor != '\0') {
                *cursor++ = '\0';
            }
        }
    }
    return count == expected ? STATUS_OK : STATUS_INVALID_FORMAT;
}

static Status parse_positive(const char *const text, size_t *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (*text == '\0') {
        return STATUS_INVALID_FORMAT;
    }
    size_t value = 0U;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return STATUS_INVALID_FORMAT;
        }
        const size_t digit = (size_t)(*cursor - '0');
        if (value > (SIZE_MAX - digit) / 10U) {
            return STATUS_OUT_OF_RANGE;
        }
        value = value * 10U + digit;
    }
    if (value == 0U) {
        return STATUS_OUT_OF_RANGE;
    }
    *result = value;
    return STATUS_OK;
}

static Status parse_time(const char *const text, unsigned int *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (strlen(text) != 5U || text[2] != ':') {
        return STATUS_INVALID_FORMAT;
    }
    for (size_t i = 0U; i < 5U; ++i) {
        if (i != 2U && (text[i] < '0' || text[i] > '9')) {
            return STATUS_INVALID_FORMAT;
        }
    }
    const unsigned int hour = (unsigned int)(text[0] - '0') * 10U
                           + (unsigned int)(text[1] - '0');
    const unsigned int minute = (unsigned int)(text[3] - '0') * 10U
                             + (unsigned int)(text[4] - '0');
    if (hour > 23U || minute > 59U) {
        return STATUS_OUT_OF_RANGE;
    }
    *result = hour * 60U + minute;
    return STATUS_OK;
}

static Status read_input(FILE *const stream, Talk **const talks,
                         size_t *const count, size_t *const hall_count) {
    if (stream == NULL || talks == NULL || count == NULL || hall_count == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    *talks = NULL;
    *count = 0U;
    *hall_count = 0U;
    char *line = NULL;
    size_t capacity = 0U;
    size_t n = 0U;
    size_t k = 0U;
    char *tokens[3];
    Status status = read_line(stream, &line, &capacity);
    if (status == STATUS_OK) {
        status = split_line(line, tokens, 2U);
    }
    if (status == STATUS_OK) {
        status = parse_positive(tokens[0], &n);
    }
    if (status == STATUS_OK) {
        status = parse_positive(tokens[1], &k);
    }
    if (status == STATUS_OK && n > SIZE_MAX / sizeof(Talk)) {
        status = STATUS_OUT_OF_RANGE;
    }

    Talk *data = NULL;
    if (status == STATUS_OK) {
        data = malloc(n * sizeof(*data));
        if (data == NULL) {
            status = STATUS_ALLOCATION_ERROR;
        }
    }
    for (size_t i = 0U; i < n && status == STATUS_OK; ++i) {
        status = read_line(stream, &line, &capacity);
        if (status == STATUS_OK) {
            status = split_line(line, tokens, 3U);
        }
        if (status == STATUS_OK) {
            status = parse_positive(tokens[0], &data[i].id);
        }
        if (status == STATUS_OK) {
            status = parse_time(tokens[1], &data[i].start_time);
        }
        if (status == STATUS_OK) {
            status = parse_time(tokens[2], &data[i].finish_time);
        }
        if (status == STATUS_OK && data[i].start_time >= data[i].finish_time) {
            status = STATUS_INVALID_FORMAT;
        }
    }
    free(line);
    if (status != STATUS_OK) {
        free(data);
        return status;
    }
    *talks = data;
    *count = n;
    *hall_count = k;
    return STATUS_OK;
}

int main(const int argc, char *argv[]) {
    (void)argv;
    if (argc != 1) {
        fputs("Error: no command-line arguments expected; read n, k and talks from stdin.\n", stderr);
        return 1;
    }

    Talk *talks = NULL;
    size_t count = 0U;
    size_t hall_count = 0U;
    size_t result = 0U;
    Status status = read_input(stdin, &talks, &count, &hall_count);
    if (status == STATUS_OK) {
        status = max_talk_count(talks, count, hall_count, &result);
    }
    free(talks);
    if (status == STATUS_OK && (printf("%zu\n", result) < 0 || fflush(stdout) == EOF)) {
        status = STATUS_IO_ERROR;
    }

    switch (status) {
        case STATUS_OK:
            return 0;
        case STATUS_INVALID_ARGUMENT:
            fputs("Error: invalid function arguments or talk data.\n", stderr);
            break;
        case STATUS_INVALID_FORMAT:
            fputs("Error: expected n k, then n lines: id HH:MM HH:MM; start must precede finish.\n", stderr);
            break;
        case STATUS_OUT_OF_RANGE:
            fputs("Error: positive integers must fit size_t; hours 00..23, minutes 00..59.\n", stderr);
            break;
        case STATUS_DUPLICATE_ID:
            fputs("Error: talk IDs must be unique.\n", stderr);
            break;
        case STATUS_ALLOCATION_ERROR:
            fputs("Error: memory allocation failed.\n", stderr);
            break;
        case STATUS_IO_ERROR:
            fputs("Error: input/output operation failed.\n", stderr);
            break;
        case STATUS_END_OF_INPUT:
            fputs("Error: input ended before all required lines were read.\n", stderr);
            break;
        default:
            fputs("Error: unknown status.\n", stderr);
            break;
    }
    return 1;
}
