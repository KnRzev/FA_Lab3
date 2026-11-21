#include <ctype.h>
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
    STATUS_OPEN_ERROR,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT,
    STATUS_NO_CHANGE,
    STATUS_WRITE_ERROR,
    STATUS_TIME_ERROR
} Status;

typedef struct {
    int day;
    int month;
    int year;
} Date;

typedef struct {
    char *surname;
    char *name;
    char *patronymic;
    Date birth;
    char gender;
    double income;
} Liver;

typedef struct LiverNode {
    Liver value;

    struct LiverNode *next;
} LiverNode;

typedef struct {
    LiverNode *head;
    size_t count;
} LiverList;

typedef enum {
    MOD_ADD,
    MOD_DELETE,
    MOD_EDIT
} Modification;

typedef struct Action {
    Modification kind;
    LiverNode *node;
    size_t old_index;
    Liver old_value;

    struct Action *next;
} Action;

typedef struct {
    Action *top;
    size_t count;
} History;

enum {
    FIELD_SURNAME = 1U,
    FIELD_NAME = 2U,
    FIELD_PATRONYMIC = 4U,
    FIELD_BIRTH = 8U,
    FIELD_GENDER = 16U,
    FIELD_INCOME = 32U,
    FIELD_ALL = 63U
};

typedef struct {
    unsigned int fields;
    Liver value;
} LiverQuery;

typedef struct {
    const LiverNode *node;
    size_t index;
} LiverMatch;

typedef struct {
    LiverMatch *items;
    size_t count;
} LiverMatches;

static Status date_validate(const Date *const date) {
    if (date == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (date->year < 1 || date->year > 9999 || date->month < 1 || date->month > 12
        || date->day < 1) {
        return STATUS_INVALID_DATA;
    }

    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int leap = date->year % 400 == 0 || (date->year % 4 == 0 && date->year % 100 != 0);

    return date->day <= days[date->month - 1] + (date->month == 2 && leap)
        ? STATUS_OK
        : STATUS_INVALID_DATA;
}

static Status date_compare(const Date *const a, const Date *const b, int *const result) {
    if (a == NULL || b == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (a->year != b->year) {
        *result = (a->year > b->year) - (a->year < b->year);
    } else if (a->month != b->month) {
        *result = (a->month > b->month) - (a->month < b->month);
    } else {
        *result = (a->day > b->day) - (a->day < b->day);
    }

    return STATUS_OK;
}

Status parse_date(const char *const text, Date *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (strlen(text) != 10U || text[2] != '.' || text[5] != '.') {
        return STATUS_INVALID_DATA;
    }

    for (size_t i = 0; i < 10U; ++i) {
        if (i != 2U && i != 5U && !isdigit((unsigned char)text[i])) {
            return STATUS_INVALID_DATA;
        }
    }

    const Date date = {
        (text[0] - '0') * 10 + text[1] - '0',
        (text[3] - '0') * 10 + text[4] - '0',
        (text[6] - '0') * 1000 + (text[7] - '0') * 100 + (text[8] - '0') * 10 + text[9] - '0'
    };

    const Status status = date_validate(&date);

    if (status == STATUS_OK) {
        *result = date;
    }

    return status;
}

static Status validate_letters(const char *const text, const int allow_empty) {
    if (text == NULL || (allow_empty != 0 && allow_empty != 1)) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (!allow_empty && *text == '\0') {
        return STATUS_INVALID_DATA;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z'))) {
            return STATUS_INVALID_DATA;
        }
    }

    return STATUS_OK;
}

static Status validate_income(const double income) {
    return !isfinite(income) || (signbit(income) && fpclassify(income) != FP_ZERO)
        ? STATUS_INVALID_DATA
        : STATUS_OK;
}

static Status validate_epsilon(const double epsilon) {
    return !isfinite(epsilon) || signbit(epsilon) || fpclassify(epsilon) == FP_ZERO
        ? STATUS_INVALID_ARGUMENT
        : STATUS_OK;
}

static Status liver_validate(const Liver *const liver, const Date *const today) {
    if (liver == NULL || date_validate(today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    int order = 0;

    if (validate_letters(liver->surname, 0) != STATUS_OK
        || validate_letters(liver->name, 0) != STATUS_OK
        || validate_letters(liver->patronymic, 1) != STATUS_OK
        || (liver->gender != 'M' && liver->gender != 'W')
        || validate_income(liver->income) != STATUS_OK || date_validate(&liver->birth) != STATUS_OK
        || date_compare(&liver->birth, today, &order) != STATUS_OK || order > 0) {
        return STATUS_INVALID_DATA;
    }

    return STATUS_OK;
}

Status liver_destroy(Liver *const liver) {
    if (liver == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    free(liver->surname);
    free(liver->name);
    free(liver->patronymic);
    *liver = (Liver){0};

    return STATUS_OK;
}

static Status duplicate_text(const char *const text, char **const result) {
    if (text == NULL || result == NULL || *result != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const size_t length = strlen(text);

    if (length == SIZE_MAX) {
        return STATUS_OUT_OF_RANGE;
    }

    char *const copy = malloc(length + 1U);

    if (copy == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    memcpy(copy, text, length + 1U);
    *result = copy;

    return STATUS_OK;
}

static Status liver_clone(const Liver *const source, Liver *const result) {
    if (source == NULL || result == NULL || result->surname != NULL || result->name != NULL
        || result->patronymic != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Liver copy = {0};
    const char *const from[] = {source->surname, source->name, source->patronymic};
    char **const to[] = {&copy.surname, &copy.name, &copy.patronymic};
    Status status = STATUS_OK;

    for (size_t i = 0; i < 3U && status == STATUS_OK; ++i) {
        status = duplicate_text(from[i], to[i]);
    }

    if (status != STATUS_OK) {
        liver_destroy(&copy);
    } else {
        copy.birth = source->birth;
        copy.gender = source->gender;
        copy.income = fpclassify(source->income) == FP_ZERO ? 0.0 : source->income;
        *result = copy;
    }

    return status;
}

static Status list_validate(const LiverList *const list) {
    if (list == NULL || ((list->head == NULL) != (list->count == 0U))) {
        return STATUS_INVALID_ARGUMENT;
    }

    return list->count > SIZE_MAX / sizeof(LiverNode) ? STATUS_OUT_OF_RANGE : STATUS_OK;
}

static Status history_validate(const History *const history) {
    if (history == NULL || ((history->top == NULL) != (history->count == 0U))) {
        return STATUS_INVALID_ARGUMENT;
    }

    return history->count > SIZE_MAX / sizeof(Action) ? STATUS_OUT_OF_RANGE : STATUS_OK;
}

Status list_destroy(LiverList *const list) {
    if (list == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (list->head != NULL) {
        LiverNode *const node = list->head;

        list->head = node->next;
        liver_destroy(&node->value);
        free(node);
    }

    list->count = 0;

    return STATUS_OK;
}

static Status action_destroy(Action *const action) {
    if (action == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (action->kind == MOD_DELETE && action->node != NULL) {
        liver_destroy(&action->node->value);
        free(action->node);
    } else if (action->kind == MOD_EDIT) {
        liver_destroy(&action->old_value);
    }

    free(action);

    return STATUS_OK;
}

Status history_destroy(History *const history) {
    if (history == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (history->top != NULL) {
        Action *const action = history->top;

        history->top = action->next;
        action_destroy(action);
    }

    history->count = 0;

    return STATUS_OK;
}

static Status index_link(LiverList *const list, const size_t index, LiverNode ***const result) {
    if (list == NULL || result == NULL || index == 0U) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (index - 1U > list->count) {
        return STATUS_NOT_FOUND;
    }

    LiverNode **link = &list->head;

    for (size_t i = 1; i < index; ++i) {
        if (*link == NULL) {
            return STATUS_INVALID_DATA;
        }

        link = &(*link)->next;
    }

    *result = link;

    return STATUS_OK;
}

static Status sorted_link(LiverList *const list, const Date *const birth,
                          LiverNode ***const result) {
    if (list == NULL || birth == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    LiverNode **link = &list->head;

    while (*link != NULL) {
        int order = 0;
        const Status status = date_compare(&(*link)->value.birth, birth, &order);

        if (status != STATUS_OK) {
            return status;
        }

        if (order < 0) {
            break;
        }

        link = &(*link)->next;
    }

    *result = link;

    return STATUS_OK;
}

static Status liver_matches(const Liver *const liver, const LiverQuery *const query,
                            const double epsilon, int *const result) {
    if (liver == NULL || query == NULL || result == NULL
        || validate_epsilon(epsilon) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    const Liver *const value = &query->value;
    int order = 0;

    if (query->fields & FIELD_BIRTH) {
        const Status status = date_compare(&liver->birth, &value->birth, &order);

        if (status != STATUS_OK) {
            return status;
        }
    }

    *result = (!(query->fields & FIELD_SURNAME) || strcmp(liver->surname, value->surname) == 0)
        && (!(query->fields & FIELD_NAME) || strcmp(liver->name, value->name) == 0)
        && (!(query->fields & FIELD_PATRONYMIC)
            || strcmp(liver->patronymic, value->patronymic) == 0)
        && (!(query->fields & FIELD_BIRTH) || order == 0)
        && (!(query->fields & FIELD_GENDER) || liver->gender == value->gender)
        && (!(query->fields & FIELD_INCOME)
            || fabsl((long double)liver->income - (long double)value->income)
                <= (long double)epsilon);

    return STATUS_OK;
}

Status add_liver(LiverList *const list, History *const history, const Liver *const liver,
                 const Date *const today) {
    if (list_validate(list) != STATUS_OK || history_validate(history) != STATUS_OK
        || liver_validate(liver, today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (list->count == SIZE_MAX / sizeof(LiverNode)
        || history->count == SIZE_MAX / sizeof(Action)) {
        return STATUS_OUT_OF_RANGE;
    }

    LiverNode **link = NULL;
    Status status = sorted_link(list, &liver->birth, &link);

    if (status != STATUS_OK) {
        return status;
    }

    LiverNode *const node = malloc(sizeof(*node));

    if (node == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    *node = (LiverNode){0};
    status = liver_clone(liver, &node->value);

    if (status != STATUS_OK) {
        free(node);

        return status;
    }

    Action *const action = malloc(sizeof(*action));

    if (action == NULL) {
        liver_destroy(&node->value);
        free(node);

        return STATUS_ALLOCATION_ERROR;
    }

    *action = (Action){MOD_ADD, node, 0, {0}, history->top};
    node->next = *link;
    *link = node;
    ++list->count;
    history->top = action;
    ++history->count;

    return STATUS_OK;
}

Status delete_liver(LiverList *const list, History *const history, const size_t index,
                    const Date *const today) {
    if (list_validate(list) != STATUS_OK || history_validate(history) != STATUS_OK
        || date_validate(today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (history->count == SIZE_MAX / sizeof(Action)) {
        return STATUS_OUT_OF_RANGE;
    }

    LiverNode **link = NULL;
    const Status status = index_link(list, index, &link);

    if (status != STATUS_OK) {
        return status;
    }

    if (*link == NULL) {
        return STATUS_NOT_FOUND;
    }

    Action *const action = malloc(sizeof(*action));

    if (action == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    LiverNode *const node = *link;

    *action = (Action){MOD_DELETE, node, index, {0}, history->top};
    *link = node->next;
    node->next = NULL;
    --list->count;
    history->top = action;
    ++history->count;

    return STATUS_OK;
}

Status modify_liver(LiverList *const list, History *const history, const size_t index,
                    const Liver *const replacement, const Date *const today, const double epsilon) {
    if (list_validate(list) != STATUS_OK || history_validate(history) != STATUS_OK
        || liver_validate(replacement, today) != STATUS_OK
        || validate_epsilon(epsilon) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (history->count == SIZE_MAX / sizeof(Action)) {
        return STATUS_OUT_OF_RANGE;
    }

    LiverNode **old_link = NULL;
    Status status = index_link(list, index, &old_link);

    if (status != STATUS_OK) {
        return status;
    }

    if (*old_link == NULL) {
        return STATUS_NOT_FOUND;
    }

    LiverNode *const node = *old_link;
    const LiverQuery query = {FIELD_ALL, *replacement};
    int equal = 0;

    status = liver_matches(&node->value, &query, epsilon, &equal);

    if (status != STATUS_OK || equal) {
        return status == STATUS_OK ? STATUS_NO_CHANGE : status;
    }

    Liver copy = {0};

    status = liver_clone(replacement, &copy);

    if (status != STATUS_OK) {
        return status;
    }

    Action *const action = malloc(sizeof(*action));

    if (action == NULL) {
        liver_destroy(&copy);

        return STATUS_ALLOCATION_ERROR;
    }

    int birth_order = 0;

    status = date_compare(&node->value.birth, &copy.birth, &birth_order);

    if (status != STATUS_OK) {
        liver_destroy(&copy);
        free(action);

        return status;
    }

    if (birth_order != 0) {
        *old_link = node->next;
        --list->count;
        LiverNode **new_link = NULL;

        status = sorted_link(list, &copy.birth, &new_link);

        if (status != STATUS_OK) {
            node->next = *old_link;
            *old_link = node;
            ++list->count;
            liver_destroy(&copy);
            free(action);

            return status;
        }

        node->next = *new_link;
        *new_link = node;
        ++list->count;
    }

    *action = (Action){MOD_EDIT, node, index, node->value, history->top};
    node->value = copy;
    history->top = action;
    ++history->count;

    return STATUS_OK;
}

static Status undo_one(LiverList *const list, Action *const action) {
    if (list == NULL || action == NULL || action->node == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    LiverNode *const node = action->node;

    if (action->kind == MOD_DELETE) {
        LiverNode **link = NULL;
        const Status status = index_link(list, action->old_index, &link);

        if (status != STATUS_OK) {
            return status;
        }

        node->next = *link;
        *link = node;
        ++list->count;
        action->node = NULL;

        return STATUS_OK;
    }

    if (action->kind != MOD_ADD && action->kind != MOD_EDIT) {
        return STATUS_INVALID_DATA;
    }

    LiverNode **link = &list->head;

    while (*link != NULL && *link != node) {
        link = &(*link)->next;
    }

    if (*link == NULL) {
        return STATUS_INVALID_DATA;
    }

    *link = node->next;
    --list->count;

    if (action->kind == MOD_ADD) {
        liver_destroy(&node->value);
        free(node);
        action->node = NULL;
    } else {
        LiverNode **target = NULL;
        const Status status = index_link(list, action->old_index, &target);

        if (status != STATUS_OK) {
            node->next = *link;
            *link = node;
            ++list->count;

            return status;
        }

        liver_destroy(&node->value);
        node->value = action->old_value;
        action->old_value = (Liver){0};
        node->next = *target;
        *target = node;
        ++list->count;
    }

    return STATUS_OK;
}

Status undo_changes(LiverList *const list, History *const history, const Date *const today,
                    size_t *const undone) {
    if (undone == NULL || list_validate(list) != STATUS_OK || history_validate(history) != STATUS_OK
        || date_validate(today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    *undone = 0;
    const size_t target = history->count / 2U;

    while (*undone < target) {
        Action *const action = history->top;
        const Status status = undo_one(list, action);

        if (status != STATUS_OK) {
            return status;
        }

        history->top = action->next;
        --history->count;
        ++*undone;
        action_destroy(action);
    }

    return history_destroy(history);
}

static Status query_validate(const LiverQuery *const query, const Date *const today,
                             const double epsilon) {
    if (query == NULL || query->fields == 0U || (query->fields & ~(unsigned int)FIELD_ALL) != 0U
        || date_validate(today) != STATUS_OK || validate_epsilon(epsilon) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    const Liver *const value = &query->value;
    int order = 0;

    if (((query->fields & FIELD_SURNAME) && validate_letters(value->surname, 0) != STATUS_OK)
        || ((query->fields & FIELD_NAME) && validate_letters(value->name, 0) != STATUS_OK)
        || ((query->fields & FIELD_PATRONYMIC)
            && validate_letters(value->patronymic, 1) != STATUS_OK)
        || ((query->fields & FIELD_BIRTH)
            && (date_validate(&value->birth) != STATUS_OK
                || date_compare(&value->birth, today, &order) != STATUS_OK || order > 0))
        || ((query->fields & FIELD_GENDER) && value->gender != 'M' && value->gender != 'W')
        || ((query->fields & FIELD_INCOME) && validate_income(value->income) != STATUS_OK)) {
        return STATUS_INVALID_DATA;
    }

    return STATUS_OK;
}

Status matches_destroy(LiverMatches *const matches) {
    if (matches == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    free(matches->items);
    *matches = (LiverMatches){0};

    return STATUS_OK;
}

Status find_livers(const LiverList *const list, const LiverQuery *const query,
                   const Date *const today, const double epsilon, LiverMatches *const result) {
    if (result == NULL || result->items != NULL || result->count != 0U
        || list_validate(list) != STATUS_OK || query_validate(query, today, epsilon) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (list->count == 0U) {
        return STATUS_OK;
    }

    if (list->count > SIZE_MAX / sizeof(LiverMatch)) {
        return STATUS_OUT_OF_RANGE;
    }

    LiverMatch *const matches = malloc(list->count * sizeof(*matches));

    if (matches == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    size_t used = 0;
    size_t index = 1;

    for (const LiverNode *node = list->head; node != NULL; node = node->next, ++index) {
        int match = 0;
        const Status status = liver_matches(&node->value, query, epsilon, &match);

        if (status != STATUS_OK) {
            free(matches);

            return status;
        }

        if (match) {
            matches[used++] = (LiverMatch){node, index};
        }
    }

    if (used == 0U) {
        free(matches);
    } else {
        *result = (LiverMatches){matches, used};
    }

    return STATUS_OK;
}

static Status read_line(FILE *const input, char **const buffer, size_t *const capacity) {
    if (input == NULL || buffer == NULL || capacity == NULL
        || ((*buffer == NULL) != (*capacity == 0U))) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (*buffer == NULL) {
        *buffer = malloc(128U);

        if (*buffer == NULL) {
            return STATUS_ALLOCATION_ERROR;
        }

        *capacity = 128U;
    }

    size_t length = 0;
    int character = 0;
    Status status = STATUS_OK;

    while ((character = fgetc(input)) != EOF && character != '\n') {
        if (status != STATUS_OK) {
            continue;
        }

        if (character == '\0') {
            status = STATUS_INVALID_DATA;
        } else {
            if (length + 1U == *capacity) {
                if (*capacity > SIZE_MAX / 2U) {
                    status = STATUS_OUT_OF_RANGE;
                } else {
                    const size_t next = *capacity * 2U;
                    char *const resized = realloc(*buffer, next);

                    if (resized == NULL) {
                        status = STATUS_ALLOCATION_ERROR;
                    } else {
                        *buffer = resized;
                        *capacity = next;
                    }
                }
            }

            if (status == STATUS_OK) {
                (*buffer)[length++] = (char)character;
            }
        }
    }

    if (ferror(input)) {
        return STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        return status;
    }

    if (length != 0U && (*buffer)[length - 1U] == '\r') {
        --length;
    }

    (*buffer)[length] = '\0';

    return character == EOF && length == 0U ? STATUS_END_OF_INPUT : STATUS_OK;
}

static Status split_fields(char *const line, char **const fields, const size_t expected) {
    if (line == NULL || fields == NULL || expected == 0U) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *read = line;
    char *write = line;

    for (size_t i = 0; i < expected; ++i) {
        while (isspace((unsigned char)*read)) {
            ++read;
        }

        if (*read == '\0') {
            return STATUS_INVALID_DATA;
        }

        fields[i] = write;

        if (*read == '"') {
            ++read;

            while (*read != '\0' && *read != '"') {
                if (*read == '\\') {
                    ++read;

                    if (*read != '\\' && *read != '"') {
                        return STATUS_INVALID_DATA;
                    }
                }

                *write++ = *read++;
            }

            if (*read != '"') {
                return STATUS_INVALID_DATA;
            }

            ++read;

            if (*read != '\0' && !isspace((unsigned char)*read)) {
                return STATUS_INVALID_DATA;
            }
        } else {
            while (*read != '\0' && !isspace((unsigned char)*read)) {
                if (*read == '"') {
                    return STATUS_INVALID_DATA;
                }

                *write++ = *read++;
            }
        }

        while (isspace((unsigned char)*read)) {
            ++read;
        }

        *write++ = '\0';
    }

    return *read == '\0' ? STATUS_OK : STATUS_INVALID_DATA;
}

static Status parse_income(const char *const text, double *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (*text == '-') {
        return STATUS_INVALID_DATA;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (!isdigit((unsigned char)*cursor) && strchr("+-.eE", *cursor) == NULL) {
            return STATUS_INVALID_DATA;
        }
    }

    char *end = NULL;
    const double income = strtod(text, &end);

    if (end == text || *end != '\0') {
        return STATUS_INVALID_DATA;
    }

    if (!isfinite(income)) {
        return STATUS_OUT_OF_RANGE;
    }

    if (validate_income(income) != STATUS_OK) {
        return STATUS_INVALID_DATA;
    }

    *result = income;

    return STATUS_OK;
}

static Status parse_liver(char *const line, const Date *const today, Liver *const result) {
    if (line == NULL || result == NULL || result->surname != NULL || result->name != NULL
        || result->patronymic != NULL || date_validate(today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *fields[6];
    Status status = split_fields(line, fields, 6U);

    if (status != STATUS_OK) {
        return status;
    }

    Liver view = {fields[0], fields[1], fields[2], {0}, fields[4][0], 0.0};

    status = parse_date(fields[3], &view.birth);

    if (status == STATUS_OK && strlen(fields[4]) != 1U) {
        status = STATUS_INVALID_DATA;
    }

    if (status == STATUS_OK) {
        status = parse_income(fields[5], &view.income);
    }

    if (status == STATUS_OK) {
        status = liver_validate(&view, today);
    }

    return status == STATUS_OK ? liver_clone(&view, result) : status;
}

Status load_livers(const char *const path, const Date *const today, LiverList *const result) {
    if (path == NULL || *path == '\0' || result == NULL || result->head != NULL
        || result->count != 0U || date_validate(today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    FILE *const input = fopen(path, "r");

    if (input == NULL) {
        return STATUS_OPEN_ERROR;
    }

    LiverList list = {0};
    char *line = NULL;
    size_t capacity = 0;
    Status status = STATUS_OK;

    while (status == STATUS_OK) {
        status = read_line(input, &line, &capacity);

        if (status == STATUS_END_OF_INPUT) {
            status = STATUS_OK;
            break;
        }

        if (status != STATUS_OK) {
            break;
        }

        const char *cursor = line;

        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }

        if (*cursor == '\0') {
            continue;
        }

        Liver liver = {0};

        status = parse_liver(line, today, &liver);

        if (status == STATUS_OK && list.count == SIZE_MAX / sizeof(LiverNode)) {
            status = STATUS_OUT_OF_RANGE;
        }

        LiverNode **link = NULL;

        if (status == STATUS_OK) {
            status = sorted_link(&list, &liver.birth, &link);
        }

        if (status == STATUS_OK) {
            LiverNode *const node = malloc(sizeof(*node));

            if (node == NULL) {
                status = STATUS_ALLOCATION_ERROR;
            } else {
                *node = (LiverNode){liver, *link};
                *link = node;
                ++list.count;
                liver = (Liver){0};
            }
        }

        liver_destroy(&liver);
    }

    free(line);

    if (fclose(input) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        list_destroy(&list);
    } else {
        *result = list;
    }

    return status;
}

static Status write_liver(FILE *const output, const Liver *const liver) {
    if (output == NULL || liver == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const double income = fpclassify(liver->income) == FP_ZERO ? 0.0 : liver->income;

    return fprintf(output, "%s %s %s %02d.%02d.%04d %c %.17g\n", liver->surname, liver->name,
                   *liver->patronymic == '\0' ? "\"\"" : liver->patronymic, liver->birth.day,
                   liver->birth.month, liver->birth.year, liver->gender, income)
            < 0
        ? STATUS_IO_ERROR
        : STATUS_OK;
}

Status save_livers(const char *const path, const LiverList *const list, const Date *const today) {
    if (path == NULL || *path == '\0' || list_validate(list) != STATUS_OK
        || date_validate(today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    FILE *const output = fopen(path, "w");

    if (output == NULL) {
        return STATUS_OPEN_ERROR;
    }

    Status status = STATUS_OK;

    for (const LiverNode *node = list->head; node != NULL && status == STATUS_OK;
         node = node->next) {
        status = write_liver(output, &node->value);
    }

    if (status == STATUS_IO_ERROR) {
        status = STATUS_WRITE_ERROR;
    }

    if (fclose(output) == EOF) {
        status = STATUS_WRITE_ERROR;
    }

    return status;
}

static Status current_date(Date *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const time_t now = time(NULL);

    if (now == (time_t)-1) {
        return STATUS_TIME_ERROR;
    }

    const struct tm *const local = localtime(&now);

    if (local == NULL || local->tm_year < -1899 || local->tm_year > 8099) {
        return STATUS_TIME_ERROR;
    }

    *result = (Date){local->tm_mday, local->tm_mon + 1, local->tm_year + 1900};

    return date_validate(result) == STATUS_OK ? STATUS_OK : STATUS_TIME_ERROR;
}

static Status read_prompt(const char *const prompt, char **const line, size_t *const capacity) {
    if (prompt == NULL || line == NULL || capacity == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (fputs(prompt, stdout) == EOF || fflush(stdout) == EOF) {
        return STATUS_IO_ERROR;
    }

    return read_line(stdin, line, capacity);
}

static Status read_number(const char *const prompt, const size_t minimum, const size_t maximum,
                          size_t *const result) {
    if (result == NULL || minimum > maximum) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *line = NULL;
    size_t capacity = 0;
    size_t value = 0;
    Status status = read_prompt(prompt, &line, &capacity);

    if (status == STATUS_OK && *line == '\0') {
        status = STATUS_INVALID_DATA;
    }

    for (const char *cursor = line; status == STATUS_OK && *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            status = STATUS_INVALID_DATA;
        } else {
            const size_t digit = (size_t)(*cursor - '0');

            if (value > (SIZE_MAX - digit) / 10U) {
                status = STATUS_OUT_OF_RANGE;
            } else {
                value = value * 10U + digit;
            }
        }
    }

    if (status == STATUS_OK && (value < minimum || value > maximum)) {
        status = STATUS_OUT_OF_RANGE;
    }

    free(line);

    if (status == STATUS_OK) {
        *result = value;
    }

    return status;
}

static Status read_liver(const Date *const today, Liver *const result) {
    if (result == NULL || date_validate(today) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *line = NULL;
    size_t capacity = 0;

    Status status = read_prompt(
        "Record: surname name patronymic dd.MM.yyyy M/W income (empty patronymic: \"\"): ", &line,
        &capacity);

    if (status == STATUS_OK) {
        status = parse_liver(line, today, result);
    }

    free(line);

    return status;
}

static Status read_query(const Date *const today, const double epsilon, LiverQuery *const result) {
    if (result == NULL || result->fields != 0U || result->value.surname != NULL
        || result->value.name != NULL || result->value.patronymic != NULL
        || date_validate(today) != STATUS_OK || validate_epsilon(epsilon) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    LiverQuery query = {0};
    char *line = NULL;
    size_t capacity = 0;

    Status status = read_prompt(
        "Search fields: 1 surname, 2 name, 3 patronymic, 4 birth, 5 gender, 6 income; 7 all\n"
        "Enter one or more field numbers separated by spaces: ",
        &line, &capacity);
    const char *cursor = line;

    while (status == STATUS_OK) {
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        if (*cursor < '1' || *cursor > '7') {
            status = STATUS_INVALID_DATA;
            break;
        }

        const unsigned int field = (unsigned int)(*cursor++ - '1');

        if (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
            status = STATUS_INVALID_DATA;
            break;
        }

        if (field == 6U) {
            while (isspace((unsigned char)*cursor)) {
                ++cursor;
            }

            if (query.fields != 0U || *cursor != '\0') {
                status = STATUS_INVALID_DATA;
            } else {
                query.fields = FIELD_ALL;
            }

            break;
        }

        if (query.fields & (1U << field)) {
            status = STATUS_INVALID_DATA;
            break;
        }

        query.fields |= 1U << field;
    }

    if (status == STATUS_OK && query.fields == 0U) {
        status = STATUS_INVALID_DATA;
    }

    const char *const prompts[] = {
        "Surname: ",
        "Name: ",
        "Patronymic (may be empty): ",
        "Birth date (dd.MM.yyyy): ",
        "Gender (M/W): ",
        "Income: "
    };

    char **const names[] = {&query.value.surname, &query.value.name, &query.value.patronymic};

    for (unsigned int field = 0; field < 6U && status == STATUS_OK; ++field) {
        if (!(query.fields & (1U << field))) {
            continue;
        }

        status = read_prompt(prompts[field], &line, &capacity);

        if (status != STATUS_OK) {
            break;
        }

        if (field < 3U) {
            status = duplicate_text(line, names[field]);
        } else if (field == 3U) {
            status = parse_date(line, &query.value.birth);
        } else if (field == 4U) {
            if (strlen(line) != 1U || (line[0] != 'M' && line[0] != 'W')) {
                status = STATUS_INVALID_DATA;
            } else {
                query.value.gender = line[0];
            }
        } else {
            status = parse_income(line, &query.value.income);
        }
    }

    if (status == STATUS_OK) {
        status = query_validate(&query, today, epsilon);
    }

    free(line);

    if (status != STATUS_OK) {
        liver_destroy(&query.value);
    } else {
        *result = query;
    }

    return status;
}

static Status print_list(const LiverList *const list) {
    if (list_validate(list) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (list->count == 0U) {
        return puts("No residents.") == EOF ? STATUS_IO_ERROR : STATUS_OK;
    }

    size_t index = 1;

    for (const LiverNode *node = list->head; node != NULL; node = node->next, ++index) {
        if (printf("%zu: ", index) < 0) {
            return STATUS_IO_ERROR;
        }

        const Status status = write_liver(stdout, &node->value);

        if (status != STATUS_OK) {
            return status;
        }
    }

    return STATUS_OK;
}

static Status run_command(LiverList *const list, History *const history, const Date *const today,
                          const double epsilon, const size_t command) {
    if (list == NULL || history == NULL || command < 1U || command > 7U) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (command == 1U) {
        return print_list(list);
    }

    Status status = STATUS_OK;

    if (command == 2U) {
        LiverQuery query = {0};
        LiverMatches matches = {0};

        status = read_query(today, epsilon, &query);

        if (status == STATUS_OK) {
            status = find_livers(list, &query, today, epsilon, &matches);
        }

        if (status == STATUS_OK && matches.count == 0U && puts("No matching residents.") == EOF) {
            status = STATUS_IO_ERROR;
        }

        for (size_t i = 0; i < matches.count && status == STATUS_OK; ++i) {
            if (printf("%zu: ", matches.items[i].index) < 0) {
                status = STATUS_IO_ERROR;
            } else {
                status = write_liver(stdout, &matches.items[i].node->value);
            }
        }

        matches_destroy(&matches);
        liver_destroy(&query.value);
    } else if (command == 3U || command == 4U) {
        size_t index = 0;
        Liver liver = {0};

        if (command == 4U) {
            status = read_number("Current resident index: ", 1U, SIZE_MAX, &index);
        }

        if (status == STATUS_OK) {
            status = read_liver(today, &liver);
        }

        if (status == STATUS_OK) {
            status =
                command == 3U ? add_liver(list, history, &liver, today)
                : modify_liver(list, history, index, &liver, today, epsilon);
        }

        liver_destroy(&liver);

        if (status == STATUS_NO_CHANGE) {
            return puts("No changes; history unchanged.") == EOF ? STATUS_IO_ERROR : STATUS_OK;
        }

        if (status == STATUS_OK
            && puts(command == 3U ? "Resident added." : "Resident updated.") == EOF) {
            status = STATUS_IO_ERROR;
        }
    } else if (command == 5U) {
        size_t index = 0;

        status = read_number("Current resident index: ", 1U, SIZE_MAX, &index);

        if (status == STATUS_OK) {
            status = delete_liver(list, history, index, today);
        }

        if (status == STATUS_OK && puts("Resident deleted.") == EOF) {
            status = STATUS_IO_ERROR;
        }
    } else if (command == 6U) {
        char *path = NULL;
        size_t capacity = 0;

        status = read_prompt("Output path (without quotes): ", &path, &capacity);

        if (status == STATUS_OK) {
            status = save_livers(path, list, today);
        }

        free(path);

        if (status == STATUS_OK && puts("Saved.") == EOF) {
            status = STATUS_IO_ERROR;
        }
    } else {
        size_t undone = 0;

        status = undo_changes(list, history, today, &undone);

        if (status == STATUS_OK
            && printf("Undone: %zu modifications. History reset.\n", undone) < 0) {
            status = STATUS_IO_ERROR;
        }
    }

    return status;
}

static Status print_error(const Status status) {
    const char *message = NULL;

    switch (status) {
        case STATUS_OK:
            return STATUS_OK;

        case STATUS_INVALID_ARGUMENT:
            message = "invalid arguments, resident fields or search values";
            break;

        case STATUS_INVALID_DATA:
            message = "check Latin names, birth date, M/W gender and nonnegative finite income";
            break;

        case STATUS_OUT_OF_RANGE:
            message = "number or allocation size is out of range";
            break;

        case STATUS_ALLOCATION_ERROR:
            message = "memory allocation failed";
            break;

        case STATUS_NOT_FOUND:
            message = "resident index not found";
            break;

        case STATUS_OPEN_ERROR:
            message = "cannot open the requested file";
            break;

        case STATUS_IO_ERROR:
            message = "input/output operation failed";
            break;

        case STATUS_END_OF_INPUT:
            message = "input ended";
            break;

        case STATUS_NO_CHANGE:
            message = "no changes";
            break;

        case STATUS_WRITE_ERROR:
            message = "file write failed; residents and Undo history remain in memory";
            break;

        case STATUS_TIME_ERROR:
            message = "cannot obtain the current local date";
            break;

        default:
            message = "unknown status";
            break;
    }

    return fprintf(stderr, "Error: %s.\n", message) < 0 ? STATUS_IO_ERROR : STATUS_OK;
}

int main(const int argc, char *argv[]) {
    if (argc != 2) {
        fputs("Usage: task07 residents_file\n", stderr);

        return 1;
    }

    Date today = {0};
    LiverList list = {0};
    History history = {0};
    const double epsilon = 1e-9;
    Status status = current_date(&today);

    if (status == STATUS_OK) {
        status = load_livers(argv[1], &today, &list);
    }

    while (status == STATUS_OK) {
        if (printf("\nResidents: %zu | Changes: %zu | Undo: %zu\n", list.count, history.count,
                   history.count / 2U)
                < 0
            || puts("1 List | 2 Find | 3 Add | 4 Edit | 5 Delete | 6 Save | 7 Undo | 0 Quit")
                == EOF) {
            status = STATUS_IO_ERROR;
            break;
        }

        size_t command = 0;
        Status action = read_number("Command: ", 0U, 7U, &command);

        if (action == STATUS_END_OF_INPUT || (action == STATUS_OK && command == 0U)) {
            break;
        }

        if (action == STATUS_OK) {
            action = run_command(&list, &history, &today, epsilon, command);
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

    history_destroy(&history);
    list_destroy(&list);

    if (fflush(stdout) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        print_error(status);

        return 1;
    }

    return 0;
}
