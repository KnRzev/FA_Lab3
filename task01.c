#include <limits.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_INVALID_FORMAT,
    STATUS_OUT_OF_RANGE,
    STATUS_BUFFER_TOO_SMALL,
    STATUS_IO_ERROR
} Status;

static Status bitwise_add(unsigned long long left, unsigned long long right, unsigned long long *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (right != 0ULL) {
        const unsigned long long carry = left & right;
        if (carry > (ULLONG_MAX >> 1U)) {
            return STATUS_OUT_OF_RANGE;
        }
        left ^= right;
        right = carry << 1U;
    }

    *result = left;
    return STATUS_OK;
}

static Status write_digits(const unsigned long long number,
                           const unsigned int r,
                           char *const buffer,
                           const size_t capacity,
                           unsigned long long *const position) {
    if (buffer == NULL || position == NULL || r < 1U || r > 5U) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (*position >= capacity) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    const unsigned long long remaining = number >> r;
    if (remaining != 0ULL) {
        const Status status = write_digits(remaining, r, buffer, capacity, position);
        if (status != STATUS_OK) {
            return status;
        }
    }

    unsigned long long next_position = 0ULL;
    const Status status = bitwise_add(*position, 1ULL, &next_position);
    if (status != STATUS_OK) {
        return status;
    }
    if (next_position >= capacity) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    const char *const alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
    const unsigned long long mask = ~(ULLONG_MAX << r);
    buffer[*position] = alphabet[number & mask];
    *position = next_position;
    return STATUS_OK;
}

Status to_base_power_of_two(const long long number,
                            const unsigned int r,
                            char *const buffer,
                            const size_t capacity) {
    if (buffer == NULL || capacity == 0U) {
        return STATUS_INVALID_ARGUMENT;
    }
    buffer[0] = '\0';
    if (r < 1U || r > 5U) {
        return STATUS_INVALID_ARGUMENT;
    }

    unsigned long long magnitude = (unsigned long long)number;
    unsigned long long position = 0ULL;
    Status status = STATUS_OK;

    if (number < 0LL) {
        if (capacity < 2U) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        status = bitwise_add(~magnitude, 1ULL, &magnitude);
        if (status != STATUS_OK) {
            return status;
        }
        buffer[0] = '-';
        position = 1ULL;
    }

    status = write_digits(magnitude, r, buffer, capacity, &position);
    if (status != STATUS_OK) {
        buffer[0] = '\0';
        return status;
    }

    buffer[position] = '\0';
    return STATUS_OK;
}

static Status parse_decimal(const char *const text, long long *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const char *cursor = text;
    const int negative = *cursor == '-';
    if (*cursor == '-' || *cursor == '+') {
        ++cursor;
    }
    if (*cursor == '\0') {
        return STATUS_INVALID_FORMAT;
    }

    const unsigned long long limit = negative
        ? 0ULL - (unsigned long long)LLONG_MIN
        : (unsigned long long)LLONG_MAX;
    unsigned long long value = 0ULL;

    while (*cursor != '\0') {
        if (*cursor < '0' || *cursor > '9') {
            return STATUS_INVALID_FORMAT;
        }
        const unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > (limit - digit) / 10ULL) {
            return STATUS_OUT_OF_RANGE;
        }
        value = value * 10ULL + digit;
        ++cursor;
    }

    if (negative && value == limit) {
        *result = LLONG_MIN;
    } else if (negative) {
        *result = -(long long)value;
    } else {
        *result = (long long)value;
    }
    return STATUS_OK;
}

static Status print_conversion(const long long number, const unsigned int r) {
    char buffer[sizeof(unsigned long long) * CHAR_BIT + 2U];
    const Status status = to_base_power_of_two(number, r, buffer, sizeof(buffer));
    if (status != STATUS_OK) {
        return status;
    }
    if (printf("%lld -> %s (base %u)\n", number, buffer, 1U << r) < 0) {
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

int main(const int argc, char *argv[]) {
    Status status = STATUS_OK;

    if (argc == 1) {
        const long long examples[] = {0LL, 31LL, 255LL, -255LL, LLONG_MIN, LLONG_MAX};
        const size_t count = sizeof(examples) / sizeof(examples[0]);
        for (size_t i = 0U; i < count && status == STATUS_OK; ++i) {
            for (unsigned int r = 1U; r <= 5U && status == STATUS_OK; ++r) {
                status = print_conversion(examples[i], r);
            }
        }
    } else if (argc == 3) {
        long long number = 0LL;
        long long r = 0LL;
        status = parse_decimal(argv[1], &number);
        if (status == STATUS_OK) {
            status = parse_decimal(argv[2], &r);
        }
        if (status == STATUS_OK && (r < 1LL || r > 5LL)) {
            status = STATUS_OUT_OF_RANGE;
        }
        if (status == STATUS_OK) {
            status = print_conversion(number, (unsigned int)r);
        }
    } else {
        fprintf(stderr, "Usage: %s [decimal_integer r]\nr must be from 1 to 5.\n", argv[0]);
        return 1;
    }

    if (fflush(stdout) == EOF) {
        status = STATUS_IO_ERROR;
    }

    switch (status) {
        case STATUS_OK:
            return 0;
        case STATUS_INVALID_ARGUMENT:
            fputs("Error: invalid function arguments.\n", stderr);
            break;
        case STATUS_INVALID_FORMAT:
            fputs("Error: expected a decimal integer without spaces.\n", stderr);
            break;
        case STATUS_OUT_OF_RANGE:
            fputs("Error: integer is out of range or r is outside 1..5.\n", stderr);
            break;
        case STATUS_BUFFER_TOO_SMALL:
            fputs("Error: output buffer is too small.\n", stderr);
            break;
        case STATUS_IO_ERROR:
            fputs("Error: failed to write output.\n", stderr);
            break;
        default:
            fputs("Error: unknown status.\n", stderr);
            break;
    }
    return 1;
}
