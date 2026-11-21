#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_INVALID_DATA,
    STATUS_OUT_OF_RANGE,
    STATUS_ALLOCATION_ERROR,
    STATUS_NOT_FOUND,
    STATUS_DUPLICATE_ID,
    STATUS_ALREADY_DELIVERED,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT,
    STATUS_TIME_ERROR
} Status;

typedef struct {
    char *data;
    int length;
} String;

typedef struct {
    String city;
    String street;
    unsigned int house;
    String building;
    unsigned int apartment;
    String index;
} Address;

typedef struct {
    Address recipient;
    double weight;
    String id;
    String creation_time;
    String delivery_time;
    int delivered;
} Mail;

typedef struct {
    const Address *address;
    Mail *items;
    size_t count;
    size_t capacity;
} Post;

typedef enum {
    FILTER_DELIVERED,
    FILTER_EXPIRED
} MailFilter;

typedef struct {
    const Mail *mail;
    long long creation_stamp;
} MailView;

static Status string_validate(const String *const string) {
    if (string == NULL || string->length < 0) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (string->data == NULL) {
        return string->length == 0 ? STATUS_OK : STATUS_INVALID_DATA;
    }

    return strlen(string->data) == (size_t)string->length ? STATUS_OK : STATUS_INVALID_DATA;
}

Status string_destroy(String *const string) {
    if (string == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    free(string->data);
    *string = (String){0};

    return STATUS_OK;
}

Status string_create(const char *const text, String *const result) {
    if (text == NULL || string_validate(result) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    const size_t length = strlen(text);

    if (length > INT_MAX || length == SIZE_MAX) {
        return STATUS_OUT_OF_RANGE;
    }

    char *const copy = malloc(length + 1U);

    if (copy == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    memcpy(copy, text, length + 1U);
    free(result->data);
    *result = (String){copy, (int)length};

    return STATUS_OK;
}

Status string_compare(const String *const first, const String *const second, int *const result) {
    if (result == NULL || string_validate(first) != STATUS_OK
        || string_validate(second) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (first->length != second->length) {
        *result = (first->length > second->length) - (first->length < second->length);
    } else {
        const int order = strcmp(first->data == NULL ? "" : first->data,
                                 second->data == NULL ? "" : second->data);
        *result = (order > 0) - (order < 0);
    }

    return STATUS_OK;
}

Status string_equal(const String *const first, const String *const second, int *const result) {
    if (result == NULL || string_validate(first) != STATUS_OK
        || string_validate(second) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    *result = strcmp(first->data == NULL ? "" : first->data,
                     second->data == NULL ? "" : second->data) == 0;

    return STATUS_OK;
}

Status string_copy(const String *const source, String *const destination) {
    if (string_validate(source) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (source == destination) {
        return STATUS_OK;
    }

    return string_create(source->data == NULL ? "" : source->data, destination);
}

Status string_clone(const String *const source, String **const result) {
    if (result == NULL || *result != NULL || string_validate(source) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    String *const copy = calloc(1U, sizeof(*copy));

    if (copy == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    const Status status = string_copy(source, copy);

    if (status != STATUS_OK) {
        free(copy);
    } else {
        *result = copy;
    }

    return status;
}

Status string_concat(String *const first, const String *const second) {
    if (string_validate(first) != STATUS_OK || string_validate(second) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (second->length == 0) {
        return STATUS_OK;
    }

    if (first->length > INT_MAX - second->length) {
        return STATUS_OUT_OF_RANGE;
    }

    const size_t length = (size_t)first->length + (size_t)second->length;

    if (length == SIZE_MAX) {
        return STATUS_OUT_OF_RANGE;
    }

    char *const joined = malloc(length + 1U);

    if (joined == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    if (first->length != 0) {
        memcpy(joined, first->data, (size_t)first->length);
    }

    if (second->length != 0) {
        memcpy(joined + first->length, second->data, (size_t)second->length);
    }

    joined[length] = '\0';
    free(first->data);
    *first = (String){joined, (int)length};

    return STATUS_OK;
}

static Status validate_digits(const String *const text, const int length) {
    if (string_validate(text) != STATUS_OK || length < 1) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (text->length != length) {
        return STATUS_INVALID_DATA;
    }

    for (int i = 0; i < length; ++i) {
        if (text->data[i] < '0' || text->data[i] > '9') {
            return STATUS_INVALID_DATA;
        }
    }

    return STATUS_OK;
}

static Status validate_nonblank(const String *const text) {
    if (string_validate(text) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (int i = 0; i < text->length; ++i) {
        if (!isspace((unsigned char)text->data[i])) {
            return STATUS_OK;
        }
    }

    return STATUS_INVALID_DATA;
}

static Status address_validate(const Address *const address) {
    if (address == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (validate_nonblank(&address->city) != STATUS_OK
        || validate_nonblank(&address->street) != STATUS_OK
        || string_validate(&address->building) != STATUS_OK
        || validate_digits(&address->index, 6) != STATUS_OK || address->house == 0U
        || address->apartment == 0U) {
        return STATUS_INVALID_DATA;
    }

    return STATUS_OK;
}

static Status address_destroy(Address *const address) {
    if (address == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    string_destroy(&address->city);
    string_destroy(&address->street);
    string_destroy(&address->building);
    string_destroy(&address->index);
    *address = (Address){0};

    return STATUS_OK;
}

Status parse_datetime(const String *const text, long long *const result) {
    if (result == NULL || string_validate(text) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (text->length != 19) {
        return STATUS_INVALID_DATA;
    }

    const char *const s = text->data;

    if (s[2] != ':' || s[5] != ':' || s[10] != ' ' || s[13] != ':' || s[16] != ':') {
        return STATUS_INVALID_DATA;
    }

    for (int i = 0; i < 19; ++i) {
        if (i != 2 && i != 5 && i != 10 && i != 13 && i != 16 && !isdigit((unsigned char)s[i])) {
            return STATUS_INVALID_DATA;
        }
    }

    const int day = (s[0] - '0') * 10 + s[1] - '0';
    const int month = (s[3] - '0') * 10 + s[4] - '0';
    const int year = (s[6] - '0') * 1000 + (s[7] - '0') * 100 + (s[8] - '0') * 10 + s[9] - '0';
    const int hour = (s[11] - '0') * 10 + s[12] - '0';
    const int minute = (s[14] - '0') * 10 + s[15] - '0';
    const int second = (s[17] - '0') * 10 + s[18] - '0';

    if (year == 0 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59
        || second > 59) {
        return STATUS_INVALID_DATA;
    }

    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int leap = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);

    if (day > days[month - 1] + (month == 2 && leap)) {
        return STATUS_INVALID_DATA;
    }

    *result = (((((long long)year * 100 + month) * 100 + day) * 100 + hour) * 100 + minute) * 100
        + second;

    return STATUS_OK;
}

static Status mail_validate(const Mail *const mail) {
    if (mail == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    long long creation = 0;
    long long delivery = 0;

    if (address_validate(&mail->recipient) != STATUS_OK
        || validate_digits(&mail->id, 14) != STATUS_OK || !isfinite(mail->weight)
        || (signbit(mail->weight) && fpclassify(mail->weight) != FP_ZERO)
        || (mail->delivered != 0 && mail->delivered != 1)
        || parse_datetime(&mail->creation_time, &creation) != STATUS_OK
        || parse_datetime(&mail->delivery_time, &delivery) != STATUS_OK || delivery < creation) {
        return STATUS_INVALID_DATA;
    }

    return STATUS_OK;
}

static Status mail_destroy(Mail *const mail) {
    if (mail == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    address_destroy(&mail->recipient);
    string_destroy(&mail->id);
    string_destroy(&mail->creation_time);
    string_destroy(&mail->delivery_time);
    *mail = (Mail){0};

    return STATUS_OK;
}

static Status copy_mail(const Mail *const source, Mail *const destination) {
    if (source == NULL || destination == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Mail copy = {0};

    const String *const from[] = {&source->recipient.city,
                                  &source->recipient.street,
                                  &source->recipient.building,
                                  &source->recipient.index,
                                  &source->id,
                                  &source->creation_time,
                                  &source->delivery_time};
    String *const to[] = {&copy.recipient.city,
                          &copy.recipient.street,
                          &copy.recipient.building,
                          &copy.recipient.index,
                          &copy.id,
                          &copy.creation_time,
                          &copy.delivery_time};
    Status status = STATUS_OK;

    for (size_t i = 0; i < sizeof(from) / sizeof(from[0]) && status == STATUS_OK; ++i) {
        status = string_copy(from[i], to[i]);
    }

    if (status != STATUS_OK) {
        mail_destroy(&copy);

        return status;
    }

    copy.recipient.house = source->recipient.house;
    copy.recipient.apartment = source->recipient.apartment;
    copy.weight = source->weight;
    copy.delivered = source->delivered;
    mail_destroy(destination);
    *destination = copy;

    return STATUS_OK;
}

static Status post_validate(const Post *const post) {
    if (post == NULL || post->address == NULL || post->count > post->capacity
        || post->capacity > SIZE_MAX / sizeof(Mail)
        || ((post->items == NULL) != (post->capacity == 0U))) {
        return STATUS_INVALID_ARGUMENT;
    }

    return STATUS_OK;
}

Status post_destroy(Post *const post) {
    if (post == NULL || post->count > post->capacity
        || (post->items == NULL && post->count != 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < post->count; ++i) {
        mail_destroy(&post->items[i]);
    }

    free(post->items);
    *post = (Post){0};

    return STATUS_OK;
}

static int compare_mail(const void *const left, const void *const right) {
    const Mail *const a = left;
    const Mail *const b = right;
    const int order = strcmp(a->recipient.index.data, b->recipient.index.data);

    return order != 0 ? order : strcmp(a->id.data, b->id.data);
}

static Status post_find_index(const Post *const post, const String *const id,
                              size_t *const result) {
    if (post_validate(post) != STATUS_OK || validate_digits(id, 14) != STATUS_OK
        || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < post->count; ++i) {
        if (strcmp(post->items[i].id.data, id->data) == 0) {
            *result = i;

            return STATUS_OK;
        }
    }

    return STATUS_NOT_FOUND;
}

Status post_find(const Post *const post, const String *const id, const Mail **const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    *result = NULL;
    size_t index = 0;
    const Status status = post_find_index(post, id, &index);

    if (status == STATUS_OK) {
        *result = &post->items[index];
    }

    return status;
}

Status post_add(Post *const post, const Mail *const mail) {
    if (post_validate(post) != STATUS_OK || mail_validate(mail) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < post->count; ++i) {
        if (strcmp(post->items[i].id.data, mail->id.data) == 0) {
            return STATUS_DUPLICATE_ID;
        }
    }

    const size_t limit = SIZE_MAX / sizeof(Mail);

    if (post->count == limit) {
        return STATUS_OUT_OF_RANGE;
    }

    Mail copy = {0};
    Status status = copy_mail(mail, &copy);

    if (status != STATUS_OK) {
        return status;
    }

    if (post->count == post->capacity) {
        const size_t capacity =
            post->capacity == 0U ? 4U
            : post->capacity > limit / 2U ? limit
            : post->capacity * 2U;
        Mail *const resized = realloc(post->items, capacity * sizeof(*resized));

        if (resized == NULL) {
            mail_destroy(&copy);

            return STATUS_ALLOCATION_ERROR;
        }

        post->items = resized;
        post->capacity = capacity;
    }

    post->items[post->count++] = copy;
    qsort(post->items, post->count, sizeof(*post->items), compare_mail);

    return STATUS_OK;
}

Status post_remove(Post *const post, const String *const id) {
    size_t index = 0;
    const Status status = post_find_index(post, id, &index);

    if (status != STATUS_OK) {
        return status;
    }

    mail_destroy(&post->items[index]);
    memmove(post->items + index, post->items + index + 1U,
            (post->count - index - 1U) * sizeof(*post->items));
    post->items[--post->count] = (Mail){0};

    return STATUS_OK;
}

Status post_mark_delivered(Post *const post, const String *const id, const String *const now) {
    long long current = 0;
    long long creation = 0;

    if (parse_datetime(now, &current) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    size_t index = 0;
    const Status status = post_find_index(post, id, &index);

    if (status != STATUS_OK) {
        return status;
    }

    Mail *const mail = &post->items[index];

    if (mail->delivered) {
        return STATUS_ALREADY_DELIVERED;
    }

    if (parse_datetime(&mail->creation_time, &creation) != STATUS_OK || current < creation) {
        return STATUS_INVALID_DATA;
    }

    mail->delivered = 1;

    return STATUS_OK;
}

static int compare_views(const void *const left, const void *const right) {
    const MailView *const a = left;
    const MailView *const b = right;

    if (a->creation_stamp != b->creation_stamp) {
        return (a->creation_stamp > b->creation_stamp) - (a->creation_stamp < b->creation_stamp);
    }

    return strcmp(a->mail->id.data, b->mail->id.data);
}

Status post_select(const Post *const post, const MailFilter filter, const String *const now,
                   MailView **const result, size_t *const count) {
    if (post_validate(post) != STATUS_OK || result == NULL || *result != NULL || count == NULL
        || (filter != FILTER_DELIVERED && filter != FILTER_EXPIRED)) {
        return STATUS_INVALID_ARGUMENT;
    }

    *count = 0;
    long long current = 0;

    if (filter == FILTER_EXPIRED && parse_datetime(now, &current) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (post->count == 0U) {
        return STATUS_OK;
    }

    if (post->count > SIZE_MAX / sizeof(MailView)) {
        return STATUS_OUT_OF_RANGE;
    }

    MailView *const matches = malloc(post->count * sizeof(*matches));

    if (matches == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    for (size_t i = 0; i < post->count; ++i) {
        const Mail *const mail = &post->items[i];
        long long creation = 0;
        long long delivery = 0;
        Status status = parse_datetime(&mail->creation_time, &creation);

        if (status == STATUS_OK && filter == FILTER_EXPIRED) {
            status = parse_datetime(&mail->delivery_time, &delivery);
        }

        if (status != STATUS_OK) {
            free(matches);
            *count = 0;

            return status;
        }

        if ((filter == FILTER_DELIVERED && mail->delivered)
            || (filter == FILTER_EXPIRED && !mail->delivered && delivery < current)) {
            matches[(*count)++] = (MailView){mail, creation};
        }
    }

    if (*count == 0U) {
        free(matches);
    } else {
        qsort(matches, *count, sizeof(*matches), compare_views);
        *result = matches;
    }

    return STATUS_OK;
}

static Status current_time_string(String *const result) {
    const time_t now = time(NULL);

    if (now == (time_t)-1) {
        return STATUS_TIME_ERROR;
    }

    const struct tm *const local = localtime(&now);
    char buffer[32];

    if (local == NULL || strftime(buffer, sizeof(buffer), "%d:%m:%Y %H:%M:%S", local) != 19U) {
        return STATUS_TIME_ERROR;
    }

    return string_create(buffer, result);
}

static Status read_string(const char *const prompt, String *const result) {
    if (prompt == NULL || string_validate(result) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (fputs(prompt, stdout) == EOF || fflush(stdout) == EOF) {
        return STATUS_IO_ERROR;
    }

    size_t capacity = 64U;
    size_t length = 0;
    char *data = malloc(capacity);

    if (data == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    int character = 0;
    Status status = STATUS_OK;

    while ((character = getchar()) != EOF && character != '\n') {
        if (status != STATUS_OK) {
            continue;
        }

        if (character == '\0') {
            status = STATUS_INVALID_DATA;
        } else if (length == INT_MAX) {
            status = STATUS_OUT_OF_RANGE;
        } else {
            if (length + 1U == capacity) {
                const size_t limit = (size_t)INT_MAX + 1U;
                const size_t next = capacity > limit / 2U ? limit : capacity * 2U;
                char *const resized = realloc(data, next);

                if (resized == NULL) {
                    status = STATUS_ALLOCATION_ERROR;
                } else {
                    data = resized;
                    capacity = next;
                }
            }

            if (status == STATUS_OK) {
                data[length++] = (char)character;
            }
        }
    }

    if (ferror(stdin)) {
        status = STATUS_IO_ERROR;
    } else if (status == STATUS_OK && character == EOF && length == 0U) {
        status = STATUS_END_OF_INPUT;
    }

    if (status != STATUS_OK) {
        free(data);

        return status;
    }

    if (length != 0U && data[length - 1U] == '\r') {
        --length;
    }

    data[length] = '\0';
    free(result->data);
    *result = (String){data, (int)length};

    return STATUS_OK;
}

static Status read_uint(const char *const prompt, const unsigned int minimum,
                        const unsigned int maximum, unsigned int *const result) {
    if (result == NULL || minimum > maximum) {
        return STATUS_INVALID_ARGUMENT;
    }

    String text = {0};
    Status status = read_string(prompt, &text);

    if (status == STATUS_OK && text.length == 0) {
        status = STATUS_INVALID_DATA;
    }

    unsigned int value = 0;

    for (int i = 0; i < text.length && status == STATUS_OK; ++i) {
        const unsigned int digit = (unsigned int)(text.data[i] - '0');

        if (text.data[i] < '0' || text.data[i] > '9') {
            status = STATUS_INVALID_DATA;
        } else if (value > (UINT_MAX - digit) / 10U) {
            status = STATUS_OUT_OF_RANGE;
        } else {
            value = value * 10U + digit;
        }
    }

    string_destroy(&text);

    if (status == STATUS_OK && (value < minimum || value > maximum)) {
        status = STATUS_OUT_OF_RANGE;
    }

    if (status == STATUS_OK) {
        *result = value;
    }

    return status;
}

static Status parse_weight(const String *const text, double *const result) {
    if (string_validate(text) != STATUS_OK || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (text->length == 0 || !isdigit((unsigned char)text->data[0])) {
        return STATUS_INVALID_DATA;
    }

    for (int i = 0; i < text->length; ++i) {
        if (!isdigit((unsigned char)text->data[i]) && strchr(".eE+-", text->data[i]) == NULL) {
            return STATUS_INVALID_DATA;
        }

        if (text->data[i] == '.' && !isdigit((unsigned char)text->data[i + 1])) {
            return STATUS_INVALID_DATA;
        }
    }

    char *end = NULL;
    const double weight = strtod(text->data, &end);

    if (end == text->data || *end != '\0') {
        return STATUS_INVALID_DATA;
    }

    if (!isfinite(weight)) {
        return STATUS_OUT_OF_RANGE;
    }

    *result = weight;

    return STATUS_OK;
}

static Status read_address(Address *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Address address = {0};
    Status status = read_string("City: ", &address.city);

    if (status == STATUS_OK) {
        status = read_string("Street: ", &address.street);
    }

    if (status == STATUS_OK) {
        status = read_uint("House: ", 1U, UINT_MAX, &address.house);
    }

    if (status == STATUS_OK) {
        status = read_string("Building (may be empty): ", &address.building);
    }

    if (status == STATUS_OK) {
        status = read_uint("Apartment: ", 1U, UINT_MAX, &address.apartment);
    }

    if (status == STATUS_OK) {
        status = read_string("Postal index (6 digits): ", &address.index);
    }

    if (status == STATUS_OK) {
        status = address_validate(&address);
    }

    if (status != STATUS_OK) {
        address_destroy(&address);
    } else {
        address_destroy(result);
        *result = address;
    }

    return status;
}

static Status read_mail(Mail *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Mail mail = {0};
    String weight = {0};
    Status status = read_address(&mail.recipient);

    if (status == STATUS_OK) {
        status = read_string("Weight (kg): ", &weight);
    }

    if (status == STATUS_OK) {
        status = parse_weight(&weight, &mail.weight);
    }

    if (status == STATUS_OK) {
        status = read_string("Mail ID (14 digits): ", &mail.id);
    }

    if (status == STATUS_OK) {
        status = read_string("Created (dd:MM:yyyy hh:mm:ss): ", &mail.creation_time);
    }

    if (status == STATUS_OK) {
        status = read_string("Delivery deadline (dd:MM:yyyy hh:mm:ss): ", &mail.delivery_time);
    }

    string_destroy(&weight);

    if (status != STATUS_OK) {
        mail_destroy(&mail);
    } else {
        mail_destroy(result);
        *result = mail;
    }

    return status;
}

static Status print_mail(FILE *const output, const Mail *const mail) {
    if (output == NULL || mail == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    return fprintf(output,
                   "\nID: %s\nRecipient: %s, %s, house %u, building %s, apartment %u, index %s\n"
                   "Weight: %.17g kg\nCreated: %s\nDelivery deadline: %s\nStatus: %s\n",
                   mail->id.data, mail->recipient.city.data, mail->recipient.street.data,
                   mail->recipient.house,
                   mail->recipient.building.data == NULL ? "" : mail->recipient.building.data,
                   mail->recipient.apartment, mail->recipient.index.data, mail->weight,
                   mail->creation_time.data, mail->delivery_time.data,
                   mail->delivered ? "delivered" : "not delivered")
            < 0
        ? STATUS_IO_ERROR
        : STATUS_OK;
}

static Status print_error(const Status status) {
    const char *message = NULL;

    switch (status) {
        case STATUS_OK:
            return STATUS_OK;

        case STATUS_INVALID_ARGUMENT:
            message = "invalid arguments or record fields";
            break;

        case STATUS_INVALID_DATA:
            message = "invalid address, digits, weight or calendar date";
            break;

        case STATUS_OUT_OF_RANGE:
            message = "value or size is out of range";
            break;

        case STATUS_ALLOCATION_ERROR:
            message = "memory allocation failed";
            break;

        case STATUS_NOT_FOUND:
            message = "mail not found";
            break;

        case STATUS_DUPLICATE_ID:
            message = "mail ID already exists";
            break;

        case STATUS_ALREADY_DELIVERED:
            message = "mail is already delivered";
            break;

        case STATUS_IO_ERROR:
            message = "input/output operation failed";
            break;

        case STATUS_END_OF_INPUT:
            message = "input ended";
            break;

        case STATUS_TIME_ERROR:
            message = "cannot obtain current local time";
            break;

        default:
            message = "unknown status";
            break;
    }

    return fprintf(stderr, "Error: %s.\n", message) < 0 ? STATUS_IO_ERROR : STATUS_OK;
}

static Status demonstrate_strings(void) {
    String first = {0};
    String second = {0};
    String copy = {0};
    String *clone = NULL;
    int order = 0;
    int equal = 0;
    Status status = string_create("z", &first);

    if (status == STATUS_OK) {
        status = string_create("aa", &second);
    }

    if (status == STATUS_OK) {
        status = string_compare(&first, &second, &order);
    }

    if (status == STATUS_OK) {
        status = string_copy(&first, &copy);
    }

    if (status == STATUS_OK) {
        status = string_clone(&second, &clone);
    }

    if (status == STATUS_OK) {
        status = string_equal(&first, &copy, &equal);
    }

    if (status == STATUS_OK) {
        status = string_concat(&first, &second);
    }

    if (status == STATUS_OK
        && printf("String demo: copy=%s, clone=%s, order(z,aa)=%d, "
                  "equal(z,copy)=%d, concat=%s\n",
                  copy.data, clone->data, order, equal, first.data)
            < 0) {
        status = STATUS_IO_ERROR;
    }

    string_destroy(&first);
    string_destroy(&second);
    string_destroy(&copy);

    if (clone != NULL) {
        string_destroy(clone);
        free(clone);
    }

    return status;
}

static Status run_command(Post *const post, const unsigned int command) {
    if (post == NULL || command < 1U || command > 8U) {
        return STATUS_INVALID_ARGUMENT;
    }

    Status status = STATUS_OK;

    if (command == 1U) {
        Mail mail = {0};

        status = read_mail(&mail);

        if (status == STATUS_OK) {
            status = post_add(post, &mail);
        }

        mail_destroy(&mail);

        if (status == STATUS_OK && puts("Mail added.") == EOF) {
            status = STATUS_IO_ERROR;
        }
    } else if (command == 2U || command == 3U || command == 7U) {
        String id = {0};
        String now = {0};

        status = read_string("Mail ID (14 digits): ", &id);

        if (status == STATUS_OK && command == 2U) {
            status = post_remove(post, &id);

            if (status == STATUS_OK && puts("Mail removed.") == EOF) {
                status = STATUS_IO_ERROR;
            }
        } else if (status == STATUS_OK && command == 3U) {
            const Mail *mail = NULL;

            status = post_find(post, &id, &mail);

            if (status == STATUS_OK) {
                status = print_mail(stdout, mail);
            }
        } else if (status == STATUS_OK) {
            status = current_time_string(&now);

            if (status == STATUS_OK) {
                status = post_mark_delivered(post, &id, &now);
            }

            if (status == STATUS_OK && puts("Mail marked delivered.") == EOF) {
                status = STATUS_IO_ERROR;
            }
        }

        string_destroy(&id);
        string_destroy(&now);
    } else if (command == 4U) {
        if (post->count == 0U && puts("No mail.") == EOF) {
            status = STATUS_IO_ERROR;
        }

        for (size_t i = 0; i < post->count && status == STATUS_OK; ++i) {
            status = print_mail(stdout, &post->items[i]);
        }
    } else if (command == 5U || command == 6U) {
        String now = {0};
        MailView *matches = NULL;
        size_t count = 0;

        if (command == 6U) {
            status = current_time_string(&now);
        }

        if (status == STATUS_OK) {
            status = post_select(post, command == 5U ? FILTER_DELIVERED : FILTER_EXPIRED, &now,
                                 &matches, &count);
        }

        if (status == STATUS_OK && count == 0U && puts("No matching mail.") == EOF) {
            status = STATUS_IO_ERROR;
        }

        for (size_t i = 0; i < count && status == STATUS_OK; ++i) {
            status = print_mail(stdout, matches[i].mail);
        }

        free(matches);
        string_destroy(&now);
    } else {
        status = demonstrate_strings();
    }

    return status;
}

int main(const int argc, char *argv[]) {
    (void)argv;

    if (argc != 1) {
        fputs("Usage: task04 (no command-line arguments)\n", stderr);

        return 1;
    }

    Address office = {0};
    Post post = {&office, NULL, 0U, 0U};

    Status status =
        puts("Enter the address of this post office:") == EOF ? STATUS_IO_ERROR : read_address(&office);

    while (status == STATUS_OK) {
        if (puts("\n1 Add mail | 2 Remove | 3 Find | 4 List | 5 Delivered | 6 Expired\n"
                 "7 Mark delivered | 8 String demo | 0 Quit")
            == EOF) {
            status = STATUS_IO_ERROR;
            break;
        }

        unsigned int command = 0;
        Status action = read_uint("Command: ", 0U, 8U, &command);

        if (action == STATUS_END_OF_INPUT || (action == STATUS_OK && command == 0U)) {
            break;
        }

        if (action == STATUS_OK) {
            action = run_command(&post, command);
        }

        if (action == STATUS_END_OF_INPUT) {
            break;
        }

        if (action != STATUS_OK) {
            if (print_error(action) != STATUS_OK) {
                status = STATUS_IO_ERROR;
            } else if (action == STATUS_IO_ERROR || action == STATUS_ALLOCATION_ERROR) {
                status = action;
            }
        }
    }

    post_destroy(&post);
    address_destroy(&office);

    if (status == STATUS_END_OF_INPUT) {
        status = STATUS_OK;
    }

    if (fflush(stdout) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        print_error(status);

        return 1;
    }

    return 0;
}
