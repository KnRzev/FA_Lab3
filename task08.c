#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_SYNTAX_ERROR,
    STATUS_OUT_OF_RANGE,
    STATUS_ALLOCATION_ERROR,
    STATUS_DIVISION_BY_ZERO,
    STATUS_NONINTEGER_QUOTIENT,
    STATUS_OPEN_ERROR,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT
} Status;

typedef struct Term {
    long long coefficient;
    unsigned int degree;

    struct Term *next;
} Term;

typedef struct {
    Term *head;
} Polynomial;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MULT,
    OP_DIV,
    OP_MOD,
    OP_EVAL,
    OP_DIFF,
    OP_CMPS
} Operation;

typedef struct {
    Operation operation;
    char name[5];
    char *first;
    char *second;
    unsigned int count;
} Command;

static Status checked_sum(const long long a, const long long b, const int subtract,
                          long long *const result) {
    if (result == NULL || (subtract != 0 && subtract != 1)) {
        return STATUS_INVALID_ARGUMENT;
    }

    if ((!subtract && ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)))
        || (subtract && ((b > 0 && a < LLONG_MIN + b) || (b < 0 && a > LLONG_MAX + b)))) {
        return STATUS_OUT_OF_RANGE;
    }

    *result = subtract ? a - b : a + b;

    return STATUS_OK;
}

static Status checked_product(const long long a, const long long b, long long *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if ((a > 0 && ((b > 0 && a > LLONG_MAX / b) || (b < 0 && b < LLONG_MIN / a)))
        || (a < 0 && ((b > 0 && a < LLONG_MIN / b) || (b < 0 && a < LLONG_MAX / b)))) {
        return STATUS_OUT_OF_RANGE;
    }

    *result = a * b;

    return STATUS_OK;
}

Status polynomial_destroy(Polynomial *const polynomial) {
    if (polynomial == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (polynomial->head != NULL) {
        Term *const term = polynomial->head;

        polynomial->head = term->next;
        free(term);
    }

    return STATUS_OK;
}

static Status change_term(Polynomial *const polynomial, const long long coefficient,
                          const unsigned int degree, const int subtract) {
    if (polynomial == NULL || (subtract != 0 && subtract != 1)) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (coefficient == 0) {
        return STATUS_OK;
    }

    Term **link = &polynomial->head;

    while (*link != NULL && (*link)->degree > degree) {
        link = &(*link)->next;
    }

    const int exists = *link != NULL && (*link)->degree == degree;
    long long value = 0;
    const Status status = checked_sum(exists ? (*link)->coefficient : 0, coefficient, subtract,
                                      &value);

    if (status != STATUS_OK) {
        return status;
    }

    if (exists) {
        if (value == 0) {
            Term *const removed = *link;

            *link = removed->next;
            free(removed);
        } else {
            (*link)->coefficient = value;
        }
    } else {
        Term *const created = malloc(sizeof(*created));

        if (created == NULL) {
            return STATUS_ALLOCATION_ERROR;
        }

        *created = (Term){value, degree, *link};
        *link = created;
    }

    return STATUS_OK;
}

static Status polynomial_clone(const Polynomial *const source, Polynomial *const result) {
    if (source == NULL || result == NULL || result == source || result->head != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Polynomial copy = {0};
    Status status = STATUS_OK;

    for (const Term *term = source->head; term != NULL && status == STATUS_OK; term = term->next) {
        status = change_term(&copy, term->coefficient, term->degree, 0);
    }

    if (status != STATUS_OK) {
        polynomial_destroy(&copy);
    } else {
        *result = copy;
    }

    return status;
}

static Status combine_polynomials(const Polynomial *const first, const Polynomial *const second,
                                  const int subtract, Polynomial *const result) {
    if (first == NULL || second == NULL || result == NULL || result == first || result == second
        || result->head != NULL || (subtract != 0 && subtract != 1)) {
        return STATUS_INVALID_ARGUMENT;
    }

    Polynomial sum = {0};
    const Term *a = first->head;
    const Term *b = second->head;
    Status status = STATUS_OK;

    while ((a != NULL || b != NULL) && status == STATUS_OK) {
        long long coefficient = 0;
        unsigned int degree = 0;

        if (b == NULL || (a != NULL && a->degree > b->degree)) {
            coefficient = a->coefficient;
            degree = a->degree;
            a = a->next;
        } else if (a == NULL || b->degree > a->degree) {
            status = checked_sum(0, b->coefficient, subtract, &coefficient);
            degree = b->degree;
            b = b->next;
        } else {
            status = checked_sum(a->coefficient, b->coefficient, subtract, &coefficient);
            degree = a->degree;
            a = a->next;
            b = b->next;
        }

        if (status == STATUS_OK) {
            status = change_term(&sum, coefficient, degree, 0);
        }
    }

    if (status != STATUS_OK) {
        polynomial_destroy(&sum);
    } else {
        *result = sum;
    }

    return status;
}

Status polynomial_add(const Polynomial *const first, const Polynomial *const second,
                      Polynomial *const result) {
    return combine_polynomials(first, second, 0, result);
}

Status polynomial_subtract(const Polynomial *const first, const Polynomial *const second,
                           Polynomial *const result) {
    return combine_polynomials(first, second, 1, result);
}

Status polynomial_multiply(const Polynomial *const first, const Polynomial *const second,
                           Polynomial *const result) {
    if (first == NULL || second == NULL || result == NULL || result == first || result == second
        || result->head != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Polynomial product = {0};
    Status status = STATUS_OK;

    for (const Term *a = first->head; a != NULL && status == STATUS_OK; a = a->next) {
        for (const Term *b = second->head; b != NULL && status == STATUS_OK; b = b->next) {
            long long coefficient = 0;

            if (a->degree > UINT_MAX - b->degree) {
                status = STATUS_OUT_OF_RANGE;
            } else {
                status = checked_product(a->coefficient, b->coefficient, &coefficient);

                if (status == STATUS_OK) {
                    status = change_term(&product, coefficient, a->degree + b->degree, 0);
                }
            }
        }
    }

    if (status != STATUS_OK) {
        polynomial_destroy(&product);
    } else {
        *result = product;
    }

    return status;
}

Status polynomial_divmod(const Polynomial *const first, const Polynomial *const second,
                         Polynomial *const quotient, Polynomial *const remainder) {
    if (first == NULL || second == NULL || quotient == NULL || remainder == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (quotient == remainder || quotient == first || quotient == second || remainder == first
        || remainder == second) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (quotient->head != NULL || remainder->head != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (second->head == NULL) {
        return STATUS_DIVISION_BY_ZERO;
    }

    Polynomial quotient_value = {0};
    Polynomial remainder_value = {0};
    Status status = polynomial_clone(first, &remainder_value);
    const Term *const leading = second->head;

    while (status == STATUS_OK && remainder_value.head != NULL
           && remainder_value.head->degree >= leading->degree) {
        if (remainder_value.head->coefficient == LLONG_MIN && leading->coefficient == -1) {
            status = STATUS_OUT_OF_RANGE;
        } else if (remainder_value.head->coefficient % leading->coefficient != 0) {
            status = STATUS_NONINTEGER_QUOTIENT;
        } else {
            const long long coefficient = remainder_value.head->coefficient / leading->coefficient;
            const unsigned int degree = remainder_value.head->degree - leading->degree;

            status = change_term(&quotient_value, coefficient, degree, 0);

            for (const Term *term = second->head; term != NULL && status == STATUS_OK;
                 term = term->next) {
                long long product = 0;

                status = checked_product(coefficient, term->coefficient, &product);

                if (status == STATUS_OK) {
                    status = change_term(&remainder_value, product, term->degree + degree, 1);
                }
            }
        }
    }

    if (status == STATUS_OK) {
        *quotient = quotient_value;
        *remainder = remainder_value;
    } else {
        polynomial_destroy(&quotient_value);
        polynomial_destroy(&remainder_value);
    }

    return status;
}

Status polynomial_divide(const Polynomial *const first, const Polynomial *const second,
                         Polynomial *const result) {
    Polynomial remainder = {0};
    const Status status = polynomial_divmod(first, second, result, &remainder);

    polynomial_destroy(&remainder);

    return status;
}

Status polynomial_modulo(const Polynomial *const first, const Polynomial *const second,
                         Polynomial *const result) {
    Polynomial quotient = {0};
    const Status status = polynomial_divmod(first, second, &quotient, result);

    polynomial_destroy(&quotient);

    return status;
}

Status polynomial_derivative(const Polynomial *const source, Polynomial *const result) {
    if (source == NULL || result == NULL || result == source || result->head != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Polynomial derivative = {0};
    Status status = STATUS_OK;

    for (const Term *term = source->head; term != NULL && status == STATUS_OK; term = term->next) {
        if (term->degree != 0U) {
            long long coefficient = 0;

            status = checked_product(term->coefficient, (long long)term->degree, &coefficient);

            if (status == STATUS_OK) {
                status = change_term(&derivative, coefficient, term->degree - 1U, 0);
            }
        }
    }

    if (status != STATUS_OK) {
        polynomial_destroy(&derivative);
    } else {
        *result = derivative;
    }

    return status;
}

static Status polynomial_power(const Polynomial *const source, unsigned int exponent,
                               Polynomial *const result) {
    if (source == NULL || result == NULL || result == source || result->head != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Polynomial power = {0};
    Polynomial base = {0};
    Status status = change_term(&power, 1, 0U, 0);

    if (status == STATUS_OK && exponent != 0U) {
        status = polynomial_clone(source, &base);
    }

    while (exponent != 0U && status == STATUS_OK) {
        if ((exponent & 1U) != 0U) {
            Polynomial next = {0};

            status = polynomial_multiply(&power, &base, &next);

            if (status == STATUS_OK) {
                polynomial_destroy(&power);
                power = next;
            }
        }

        exponent >>= 1U;

        if (exponent != 0U && status == STATUS_OK) {
            Polynomial next = {0};

            status = polynomial_multiply(&base, &base, &next);

            if (status == STATUS_OK) {
                polynomial_destroy(&base);
                base = next;
            }
        }
    }

    polynomial_destroy(&base);

    if (status != STATUS_OK) {
        polynomial_destroy(&power);
    } else {
        *result = power;
    }

    return status;
}

Status polynomial_compose(const Polynomial *const first, const Polynomial *const second,
                          Polynomial *const result) {
    if (first == NULL || second == NULL || result == NULL || result == first || result == second
        || result->head != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Polynomial composition = {0};
    Status status = STATUS_OK;

    for (const Term *a = first->head; a != NULL && status == STATUS_OK; a = a->next) {
        Polynomial power = {0};

        status = polynomial_power(second, a->degree, &power);

        for (const Term *b = power.head; b != NULL && status == STATUS_OK; b = b->next) {
            long long coefficient = 0;

            status = checked_product(a->coefficient, b->coefficient, &coefficient);

            if (status == STATUS_OK) {
                status = change_term(&composition, coefficient, b->degree, 0);
            }
        }

        polynomial_destroy(&power);
    }

    if (status != STATUS_OK) {
        polynomial_destroy(&composition);
    } else {
        *result = composition;
    }

    return status;
}

static Status real_power(long double base, unsigned int exponent, long double *const result) {
    if (result == NULL || !isfinite(base)) {
        return STATUS_INVALID_ARGUMENT;
    }

    long double power = 1.0L;

    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            power *= base;

            if (!isfinite(power)) {
                return STATUS_OUT_OF_RANGE;
            }
        }

        exponent >>= 1U;

        if (exponent != 0U) {
            base *= base;

            if (!isfinite(base)) {
                return STATUS_OUT_OF_RANGE;
            }
        }
    }

    *result = power;

    return STATUS_OK;
}

Status polynomial_evaluate(const Polynomial *const source, const long double point,
                           long double *const result) {
    if (source == NULL || result == NULL || !isfinite(point)) {
        return STATUS_INVALID_ARGUMENT;
    }

    long double value = 0.0L;
    unsigned int previous = source->head == NULL ? 0U : source->head->degree;
    const Term *term = source->head;
    Status status = STATUS_OK;

    while (status == STATUS_OK) {
        const unsigned int degree = term == NULL ? 0U : term->degree;

        if (fpclassify(value) != FP_ZERO) {
            long double factor = 1.0L;

            status = real_power(point, previous - degree, &factor);

            if (status == STATUS_OK) {
                value *= factor;

                if (!isfinite(value)) {
                    status = STATUS_OUT_OF_RANGE;
                }
            }
        }

        if (status != STATUS_OK || term == NULL) {
            break;
        }

        value += (long double)term->coefficient;

        if (!isfinite(value)) {
            status = STATUS_OUT_OF_RANGE;
        }

        previous = degree;
        term = term->next;
    }

    if (status == STATUS_OK) {
        *result = fpclassify(value) == FP_ZERO ? 0.0L : value;
    }

    return status;
}

static Status skip_spaces(const char **const cursor) {
    if (cursor == NULL || *cursor == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (isspace((unsigned char)**cursor)) {
        ++*cursor;
    }

    return STATUS_OK;
}

static Status parse_unsigned(const char **const cursor, const unsigned long long limit,
                             unsigned long long *const result) {
    if (cursor == NULL || *cursor == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (**cursor < '0' || **cursor > '9') {
        return STATUS_SYNTAX_ERROR;
    }

    unsigned long long value = 0;

    while (**cursor >= '0' && **cursor <= '9') {
        const unsigned int digit = (unsigned int)(**cursor - '0');

        if (digit > limit || value > (limit - digit) / 10ULL) {
            return STATUS_OUT_OF_RANGE;
        }

        value = value * 10ULL + digit;
        ++*cursor;
    }

    *result = value;

    return STATUS_OK;
}

Status polynomial_parse(const char *const text, Polynomial *const result) {
    if (text == NULL || result == NULL || result->head != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const char *cursor = text;

    skip_spaces(&cursor);
    Polynomial polynomial = {0};
    Status status = *cursor == '\0' ? STATUS_SYNTAX_ERROR : STATUS_OK;

    while (*cursor != '\0' && status == STATUS_OK) {
        int negative = 0;

        if (*cursor == '+' || *cursor == '-') {
            negative = *cursor == '-';
            ++cursor;
            skip_spaces(&cursor);
        }

        const int has_coefficient = *cursor >= '0' && *cursor <= '9';
        unsigned long long magnitude = 1;
        const unsigned long long limit = (unsigned long long)LLONG_MAX + (unsigned int)negative;

        if (has_coefficient) {
            status = parse_unsigned(&cursor, limit, &magnitude);
        }

        skip_spaces(&cursor);
        unsigned int degree = 0;

        if (status == STATUS_OK && *cursor == 'x') {
            ++cursor;
            skip_spaces(&cursor);
            degree = 1;

            if (*cursor == '^') {
                ++cursor;
                skip_spaces(&cursor);
                unsigned long long parsed = 0;

                status = parse_unsigned(&cursor, UINT_MAX, &parsed);
                degree = (unsigned int)parsed;
            }
        } else if (status == STATUS_OK && !has_coefficient) {
            status = STATUS_SYNTAX_ERROR;
        }

        if (status == STATUS_OK) {
            long long coefficient;

            if (!negative) {
                coefficient = (long long)magnitude;
            } else if (magnitude == (unsigned long long)LLONG_MAX + 1ULL) {
                coefficient = LLONG_MIN;
            } else {
                coefficient = -(long long)magnitude;
            }

            status = change_term(&polynomial, coefficient, degree, 0);
        }

        skip_spaces(&cursor);

        if (status == STATUS_OK && *cursor != '\0' && *cursor != '+' && *cursor != '-') {
            status = STATUS_SYNTAX_ERROR;
        }
    }

    if (status != STATUS_OK) {
        polynomial_destroy(&polynomial);
    } else {
        *result = polynomial;
    }

    return status;
}

static Status parse_point(const char *const text, long double *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (!isdigit((unsigned char)*cursor) && !isspace((unsigned char)*cursor)
            && strchr("+-.eE", *cursor) == NULL) {
            return STATUS_SYNTAX_ERROR;
        }
    }

    char *end = NULL;
    const long double point = strtold(text, &end);

    if (end == text) {
        return STATUS_SYNTAX_ERROR;
    }

    while (isspace((unsigned char)*end)) {
        ++end;
    }

    if (*end != '\0') {
        return STATUS_SYNTAX_ERROR;
    }

    if (!isfinite(point)) {
        return STATUS_OUT_OF_RANGE;
    }

    *result = point;

    return STATUS_OK;
}

static Status parse_command(char *const text, Command *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *cursor = text;

    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    Command command = {OP_ADD, {0}, NULL, NULL, 1U};
    size_t length = 0;

    while (isalpha((unsigned char)*cursor)) {
        if (length == sizeof(command.name) - 1U) {
            return STATUS_SYNTAX_ERROR;
        }

        command.name[length++] = *cursor++;
    }

    const char *const names[] = {"Add", "Sub", "Mult", "Div", "Mod", "Eval", "Diff", "Cmps"};
    size_t selected = 0;

    while (selected < sizeof(names) / sizeof(names[0])
           && strcmp(command.name, names[selected]) != 0) {
        ++selected;
    }

    if (selected == sizeof(names) / sizeof(names[0])) {
        return STATUS_SYNTAX_ERROR;
    }

    command.operation = (Operation)selected;

    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor != '(') {
        return STATUS_SYNTAX_ERROR;
    }

    command.first = ++cursor;

    while (*cursor != '\0' && *cursor != ')') {
        if (*cursor == '(') {
            return STATUS_SYNTAX_ERROR;
        }

        if (*cursor == ',') {
            if (command.second != NULL) {
                return STATUS_SYNTAX_ERROR;
            }

            *cursor = '\0';
            command.second = cursor + 1;
            command.count = 2;
        }

        ++cursor;
    }

    if (*cursor != ')') {
        return STATUS_SYNTAX_ERROR;
    }

    *cursor++ = '\0';

    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor != '\0' || (command.operation == OP_DIFF && command.count == 2U)) {
        return STATUS_SYNTAX_ERROR;
    }

    const char *argument = command.first;

    skip_spaces(&argument);

    if (*argument == '\0') {
        if (command.operation != OP_DIFF || command.count != 1U) {
            return STATUS_SYNTAX_ERROR;
        }

        command.count = 0;
    }

    *result = command;

    return STATUS_OK;
}

static Status execute_command(const Command *const command, Polynomial *const accumulator,
                              long double *const value, int *const numeric) {
    if (command == NULL || accumulator == NULL || value == NULL || numeric == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (command->operation < OP_ADD || command->operation > OP_CMPS
        || (command->operation == OP_DIFF ? command->count > 1U
                                          : command->count < 1U || command->count > 2U)
        || (command->count != 0U && command->first == NULL)
        || (command->count == 2U && command->second == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }

    Polynomial a = {0};
    Polynomial b = {0};
    Polynomial result = {0};
    const Polynomial *first = accumulator;
    Status status = STATUS_OK;

    *numeric = command->operation == OP_EVAL;

    if ((command->operation == OP_DIFF && command->count == 1U) || command->count == 2U) {
        status = polynomial_parse(command->first, &a);
        first = &a;
    }

    if (status == STATUS_OK && command->operation != OP_DIFF && command->operation != OP_EVAL) {
        status = polynomial_parse(command->count == 2U ? command->second : command->first, &b);
    }

    if (status == STATUS_OK) {
        switch (command->operation) {
            case OP_ADD:
                status = polynomial_add(first, &b, &result);
                break;
            case OP_SUB:
                status = polynomial_subtract(first, &b, &result);
                break;
            case OP_MULT:
                status = polynomial_multiply(first, &b, &result);
                break;
            case OP_DIV:
                status = polynomial_divide(first, &b, &result);
                break;
            case OP_MOD:
                status = polynomial_modulo(first, &b, &result);
                break;
            case OP_CMPS:
                status = polynomial_compose(first, &b, &result);
                break;
            case OP_DIFF:
                status = polynomial_derivative(first, &result);
                break;
            case OP_EVAL: {
                long double point = 0.0L;

                status = parse_point(command->count == 2U ? command->second : command->first,
                                     &point);

                if (status == STATUS_OK) {
                    status = polynomial_evaluate(first, point, value);
                }

                break;
            }

            default:
                status = STATUS_INVALID_ARGUMENT;
                break;
        }
    }

    if (status == STATUS_OK && !*numeric) {
        polynomial_destroy(accumulator);
        *accumulator = result;
        result.head = NULL;
    }

    polynomial_destroy(&a);
    polynomial_destroy(&b);
    polynomial_destroy(&result);

    return status;
}

static Status append_character(char **const buffer, size_t *const capacity, size_t *const length,
                               const int character) {
    if (buffer == NULL || capacity == NULL || length == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (*length >= SIZE_MAX - 1U) {
        return STATUS_OUT_OF_RANGE;
    }

    if (*length + 1U >= *capacity) {
        size_t next;

        if (*capacity == 0U) {
            next = 128U;
        } else if (*capacity > SIZE_MAX / 2U) {
            next = SIZE_MAX;
        } else {
            next = *capacity * 2U;
        }

        char *const resized = realloc(*buffer, next);

        if (resized == NULL) {
            return STATUS_ALLOCATION_ERROR;
        }

        *buffer = resized;
        *capacity = next;
    }

    (*buffer)[(*length)++] = (char)character;
    (*buffer)[*length] = '\0';

    return STATUS_OK;
}

static Status read_instruction(FILE *const input, char **const buffer, size_t *const capacity,
                               size_t *const line, size_t *const instruction_line) {
    if (input == NULL || buffer == NULL || capacity == NULL || line == NULL
        || instruction_line == NULL || ((*buffer == NULL) != (*capacity == 0U))) {
        return STATUS_INVALID_ARGUMENT;
    }

    enum {
        NORMAL,
        LINE_COMMENT,
        BLOCK_COMMENT
    } state = NORMAL;

    size_t length = 0;

    *instruction_line = *line;

    if (*buffer != NULL) {
        **buffer = '\0';
    }

    int character = 0;

    while ((character = fgetc(input)) != EOF) {
        if (character == '\0') {
            return STATUS_SYNTAX_ERROR;
        }

        if (character == '\n') {
            if (*line == SIZE_MAX) {
                return STATUS_OUT_OF_RANGE;
            }

            ++*line;
        }

        if (state == LINE_COMMENT) {
            if (character == '\n') {
                state = NORMAL;
            }

            continue;
        }

        if (state == BLOCK_COMMENT) {
            if (character == '[') {
                return STATUS_SYNTAX_ERROR;
            }

            if (character == ']') {
                state = NORMAL;
            }

            continue;
        }

        if (character == '[' || character == '%') {
            state = character == '[' ? BLOCK_COMMENT : LINE_COMMENT;

            if (length == 0U) {
                continue;
            }

            character = ' ';
        } else if (character == ']') {
            return STATUS_SYNTAX_ERROR;
        } else if (character == ';') {
            return length == 0U ? STATUS_SYNTAX_ERROR : STATUS_OK;
        }

        if (length == 0U) {
            if (isspace((unsigned char)character)) {
                continue;
            }

            *instruction_line = *line;
        }

        const Status status = append_character(buffer, capacity, &length, character);

        if (status != STATUS_OK) {
            return status;
        }
    }

    if (ferror(input)) {
        return STATUS_IO_ERROR;
    }

    return state == BLOCK_COMMENT || length != 0U ? STATUS_SYNTAX_ERROR : STATUS_END_OF_INPUT;
}

static Status print_polynomial(FILE *const output, const Polynomial *const polynomial) {
    if (output == NULL || polynomial == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (polynomial->head == NULL) {
        return fputc('0', output) == EOF ? STATUS_IO_ERROR : STATUS_OK;
    }

    for (const Term *term = polynomial->head; term != NULL; term = term->next) {
        const long long coefficient = term->coefficient;

        if ((coefficient < 0 && fputc('-', output) == EOF)
            || (coefficient > 0 && term != polynomial->head && fputc('+', output) == EOF)) {
            return STATUS_IO_ERROR;
        }

        const unsigned long long magnitude = coefficient < 0
            ? (unsigned long long)(-(coefficient + 1)) + 1ULL
            : (unsigned long long)coefficient;

        if ((term->degree == 0U || magnitude != 1ULL) && fprintf(output, "%llu", magnitude) < 0) {
            return STATUS_IO_ERROR;
        }

        if (term->degree != 0U && fputc('x', output) == EOF) {
            return STATUS_IO_ERROR;
        }

        if (term->degree > 1U && fprintf(output, "^%u", term->degree) < 0) {
            return STATUS_IO_ERROR;
        }
    }

    return STATUS_OK;
}

static Status print_error(const Status status, const size_t line) {
    const char *message = NULL;

    switch (status) {
        case STATUS_OK:
            return STATUS_OK;
        case STATUS_INVALID_ARGUMENT:
            message = "invalid arguments";
            break;
        case STATUS_SYNTAX_ERROR:
            message = "invalid command, polynomial or comment; check parentheses and semicolons";
            break;
        case STATUS_OUT_OF_RANGE:
            message = "coefficient, exponent or evaluation result is out of range";
            break;
        case STATUS_ALLOCATION_ERROR:
            message = "memory allocation failed";
            break;
        case STATUS_DIVISION_BY_ZERO:
            message = "division by the zero polynomial";
            break;
        case STATUS_NONINTEGER_QUOTIENT:
            message = "polynomial division requires fractional coefficients";
            break;
        case STATUS_OPEN_ERROR:
            message = "cannot open the input file";
            break;
        case STATUS_IO_ERROR:
            message = "input/output operation failed";
            break;
        case STATUS_END_OF_INPUT:
            message = "unexpected end of input";
            break;
        default:
            message = "unknown error";
            break;
    }

    return fprintf(stderr, "Error at line %zu: %s.\n", line, message) < 0 ? STATUS_IO_ERROR
                                                                          : STATUS_OK;
}

int main(const int argc, char *argv[]) {
    if (argc != 2 || argv[1][0] == '\0') {
        fputs("Usage: task08 commands_file\n", stderr);

        return 1;
    }

    FILE *const input = fopen(argv[1], "r");

    if (input == NULL) {
        print_error(STATUS_OPEN_ERROR, 1U);

        return 1;
    }

    Polynomial accumulator = {0};
    char *instruction = NULL;
    size_t capacity = 0;
    size_t line = 1;
    size_t instruction_line = 1;
    Status status = STATUS_OK;

    while (status == STATUS_OK) {
        status = read_instruction(input, &instruction, &capacity, &line, &instruction_line);

        if (status == STATUS_END_OF_INPUT) {
            status = STATUS_OK;
            break;
        }

        Command command = {0};

        if (status == STATUS_OK) {
            status = parse_command(instruction, &command);
        }

        long double value = 0.0L;
        int numeric = 0;

        if (status == STATUS_OK) {
            status = execute_command(&command, &accumulator, &value, &numeric);
        }

        if (status == STATUS_OK && printf("%s: ", command.name) < 0) {
            status = STATUS_IO_ERROR;
        }

        if (status == STATUS_OK) {
            if (numeric) {
                if (printf("%.12Lg", value) < 0) {
                    status = STATUS_IO_ERROR;
                }
            } else {
                status = print_polynomial(stdout, &accumulator);
            }
        }

        if (status == STATUS_OK && putchar('\n') == EOF) {
            status = STATUS_IO_ERROR;
        }
    }

    free(instruction);
    polynomial_destroy(&accumulator);

    if (fclose(input) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (fflush(stdout) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        print_error(status, instruction_line);

        return 1;
    }

    return 0;
}
