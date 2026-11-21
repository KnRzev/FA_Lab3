#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    EXAM_COUNT = 5,
    RECORD_FIELDS = 9
};

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_INVALID_DATA,
    STATUS_OUT_OF_RANGE,
    STATUS_ALLOCATION_ERROR,
    STATUS_OPEN_ERROR,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT,
    STATUS_EMPTY_COLLECTION,
    STATUS_PATH_CONFLICT
} Status;

typedef struct {
    unsigned int id;
    char *name;
    char *surname;
    char *group;
    unsigned char *grades;
} Student;

typedef enum {
    BY_ID,
    BY_SURNAME,
    BY_NAME,
    BY_GROUP
} StudentField;

typedef int (*StudentComparator)(const void *, const void *);

typedef struct {
    const Student **items;
    size_t count;
} StudentMatches;

static Status validate_text(const char *const text, const int letters_only) {
    if (text == NULL || (letters_only != 0 && letters_only != 1)) {
        return STATUS_INVALID_ARGUMENT;
    }

    int nonblank = 0;

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (letters_only
            && !((*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= 'a' && *cursor <= 'z'))) {
            return STATUS_INVALID_DATA;
        }

        if (!isspace((unsigned char)*cursor)) {
            nonblank = 1;
        }
    }

    return nonblank ? STATUS_OK : STATUS_INVALID_DATA;
}

static Status array_validate(const Student *const students, const size_t count) {
    if (students == NULL && count != 0U) {
        return STATUS_INVALID_ARGUMENT;
    }

    return count > SIZE_MAX / sizeof(*students) ? STATUS_OUT_OF_RANGE : STATUS_OK;
}

Status student_destroy(Student *const student) {
    if (student == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    free(student->name);
    free(student->surname);
    free(student->group);
    free(student->grades);
    *student = (Student){0};

    return STATUS_OK;
}

Status students_destroy(Student **const students, size_t *const count) {
    if (students == NULL || count == NULL || array_validate(*students, *count) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < *count; ++i) {
        student_destroy(&(*students)[i]);
    }

    free(*students);
    *students = NULL;
    *count = 0;

    return STATUS_OK;
}

Status matches_destroy(StudentMatches *const matches) {
    if (matches == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    free(matches->items);
    *matches = (StudentMatches){0};

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

static Status parse_uint(const char *const text, const unsigned int maximum,
                         unsigned int *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (*text == '\0') {
        return STATUS_INVALID_DATA;
    }

    unsigned int value = 0;

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return STATUS_INVALID_DATA;
        }

        const unsigned int digit = (unsigned int)(*cursor - '0');

        if (digit > maximum || value > (maximum - digit) / 10U) {
            return STATUS_OUT_OF_RANGE;
        }

        value = value * 10U + digit;
    }

    *result = value;

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

static Status split_record(char *const line, char *fields[RECORD_FIELDS]) {
    if (line == NULL || fields == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *read = line;
    char *write = line;

    for (size_t i = 0; i < RECORD_FIELDS; ++i) {
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

static Status parse_student(char *const line, Student *const result) {
    if (line == NULL || result == NULL || result->name != NULL || result->surname != NULL
        || result->group != NULL || result->grades != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *fields[RECORD_FIELDS];
    Student student = {0};
    unsigned char grades[EXAM_COUNT];
    Status status = split_record(line, fields);

    if (status == STATUS_OK) {
        status = parse_uint(fields[0], UINT_MAX, &student.id);
    }

    for (size_t i = 1; i <= 3U && status == STATUS_OK; ++i) {
        status = validate_text(fields[i], i != 3U);
    }

    for (size_t i = 0; i < EXAM_COUNT && status == STATUS_OK; ++i) {
        unsigned int grade = 0;

        status = parse_uint(fields[4U + i], UCHAR_MAX, &grade);

        if (status == STATUS_OK) {
            grades[i] = (unsigned char)grade;
        }
    }

    char **const strings[] = {&student.name, &student.surname, &student.group};

    for (size_t i = 0; i < 3U && status == STATUS_OK; ++i) {
        status = duplicate_text(fields[i + 1U], strings[i]);
    }

    if (status == STATUS_OK) {
        student.grades = malloc(sizeof(grades));

        if (student.grades == NULL) {
            status = STATUS_ALLOCATION_ERROR;
        } else {
            memcpy(student.grades, grades, sizeof(grades));
        }
    }

    if (status != STATUS_OK) {
        student_destroy(&student);
    } else {
        *result = student;
    }

    return status;
}

Status load_students(const char *const path, Student **const result, size_t *const count) {
    if (path == NULL || *path == '\0' || result == NULL || *result != NULL || count == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    *count = 0;
    FILE *const input = fopen(path, "r");

    if (input == NULL) {
        return STATUS_OPEN_ERROR;
    }

    Student *students = NULL;
    size_t used = 0;
    size_t allocated = 0;
    size_t capacity = 0;
    char *line = NULL;
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

        Student student = {0};

        status = parse_student(line, &student);

        if (status == STATUS_OK && used == allocated) {
            const size_t limit = SIZE_MAX / sizeof(*students);

            if (used == limit) {
                status = STATUS_OUT_OF_RANGE;
            } else {
                size_t next =
                    allocated == 0U ? 8U
                    : allocated > limit / 2U ? limit
                    : allocated * 2U;

                if (next > limit) {
                    next = limit;
                }

                Student *const resized = realloc(students, next * sizeof(*resized));

                if (resized == NULL) {
                    status = STATUS_ALLOCATION_ERROR;
                } else {
                    students = resized;
                    allocated = next;
                }
            }
        }

        if (status == STATUS_OK) {
            students[used++] = student;
        } else {
            student_destroy(&student);
        }
    }

    free(line);

    if (fclose(input) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        students_destroy(&students, &used);
    } else {
        *result = students;
        *count = used;
    }

    return status;
}

int compare_students_by_id(const void *const left, const void *const right) {
    const Student *const a = left;
    const Student *const b = right;

    return (a->id > b->id) - (a->id < b->id);
}

int compare_students_by_surname(const void *const left, const void *const right) {
    const Student *const a = left;
    const Student *const b = right;
    const int order = strcmp(a->surname, b->surname);

    return order != 0 ? order : compare_students_by_id(left, right);
}

int compare_students_by_name(const void *const left, const void *const right) {
    const Student *const a = left;
    const Student *const b = right;
    const int order = strcmp(a->name, b->name);

    return order != 0 ? order : compare_students_by_id(left, right);
}

int compare_students_by_group(const void *const left, const void *const right) {
    const Student *const a = left;
    const Student *const b = right;
    const int order = strcmp(a->group, b->group);

    return order != 0 ? order : compare_students_by_id(left, right);
}

Status sort_students(Student *const students, const size_t count,
                     const StudentComparator comparator) {
    if (comparator == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const Status status = array_validate(students, count);

    if (status == STATUS_OK && count > 1U) {
        qsort(students, count, sizeof(*students), comparator);
    }

    return status;
}

Status find_students(const Student *const students, const size_t count, const StudentField field,
                     const unsigned int id, const char *const text, StudentMatches *const result) {
    if (result == NULL || result->items != NULL || result->count != 0U || field < BY_ID
        || field > BY_GROUP) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (field != BY_ID && validate_text(text, field != BY_GROUP) != STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }

    const Status status = array_validate(students, count);

    if (status != STATUS_OK || count == 0U) {
        return status;
    }

    if (count > SIZE_MAX / sizeof(*result->items)) {
        return STATUS_OUT_OF_RANGE;
    }

    const Student **const matches = malloc(count * sizeof(*matches));

    if (matches == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    size_t used = 0;

    for (size_t i = 0; i < count; ++i) {
        const char *const value =
            field == BY_SURNAME ? students[i].surname
            : field == BY_NAME ? students[i].name
            : students[i].group;

        if ((field == BY_ID && students[i].id == id)
            || (field != BY_ID && strcmp(value, text) == 0)) {
            matches[used++] = &students[i];
        }
    }

    if (used == 0U) {
        free(matches);
    } else {
        *result = (StudentMatches){matches, used};
    }

    return STATUS_OK;
}

Status student_average(const Student *const student, double *const result) {
    if (student == NULL || student->grades == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    unsigned int sum = 0;

    for (size_t i = 0; i < EXAM_COUNT; ++i) {
        sum += student->grades[i];
    }

    *result = (double)sum / EXAM_COUNT;

    return STATUS_OK;
}

static Status overall_grade_sum(const Student *const students, const size_t count,
                                long double *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const Status status = array_validate(students, count);

    if (status != STATUS_OK) {
        return status;
    }

    *result = 0.0L;

    if (count == 0U) {
        return STATUS_EMPTY_COLLECTION;
    }

    for (size_t i = 0; i < count; ++i) {
        if (students[i].grades == NULL) {
            return STATUS_INVALID_ARGUMENT;
        }

        for (size_t j = 0; j < EXAM_COUNT; ++j) {
            *result += students[i].grades[j];
        }
    }

    return STATUS_OK;
}

Status overall_average(const Student *const students, const size_t count, double *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    *result = 0.0;
    long double sum = 0.0L;
    const Status status = overall_grade_sum(students, count, &sum);

    if (status == STATUS_OK) {
        *result = (double)(sum / ((long double)count * EXAM_COUNT));
    }

    return status;
}

Status select_above_average(const Student *const students, const size_t count, const double epsilon,
                            StudentMatches *const result, double *const average) {
    if (result == NULL || result->items != NULL || result->count != 0U || average == NULL
        || !isfinite(epsilon) || signbit(epsilon) || fpclassify(epsilon) == FP_ZERO) {
        return STATUS_INVALID_ARGUMENT;
    }

    *average = 0.0;
    long double sum = 0.0L;
    const Status status = overall_grade_sum(students, count, &sum);

    if (status != STATUS_OK) {
        return status;
    }

    *average = (double)(sum / ((long double)count * EXAM_COUNT));

    if (count > SIZE_MAX / sizeof(*result->items)) {
        return STATUS_OUT_OF_RANGE;
    }

    const Student **const matches = malloc(count * sizeof(*matches));

    if (matches == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    size_t used = 0;
    const long double threshold = (long double)epsilon * (long double)count * EXAM_COUNT;

    for (size_t i = 0; i < count; ++i) {
        unsigned int current = 0;

        for (size_t j = 0; j < EXAM_COUNT; ++j) {
            current += students[i].grades[j];
        }

        if ((long double)current * (long double)count - sum > threshold) {
            matches[used++] = &students[i];
        }
    }

    if (used == 0U) {
        free(matches);
    } else {
        *result = (StudentMatches){matches, used};
    }

    return STATUS_OK;
}

static Status print_student(FILE *const output, const Student *const student) {
    if (output == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    double average = 0.0;
    const Status status = student_average(student, &average);

    if (status != STATUS_OK) {
        return status;
    }

    return fprintf(output, "ID: %u | %s %s | group: %s | grades: %u %u %u %u %u | average: %.6f\n",
                   student->id, student->surname, student->name, student->group,
                   (unsigned int)student->grades[0], (unsigned int)student->grades[1],
                   (unsigned int)student->grades[2], (unsigned int)student->grades[3],
                   (unsigned int)student->grades[4], average)
            < 0
        ? STATUS_IO_ERROR
        : STATUS_OK;
}

static Status write_id_report(FILE *const trace, const unsigned int id,
                              const StudentMatches *const matches) {
    if (trace == NULL || matches == NULL || (matches->items == NULL && matches->count != 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (fprintf(trace, "Report by ID: %u\n", id) < 0) {
        return STATUS_IO_ERROR;
    }

    if (matches->count == 0U && fputs("No matching students.\n", trace) == EOF) {
        return STATUS_IO_ERROR;
    }

    for (size_t i = 0; i < matches->count; ++i) {
        const Student *const student = matches->items[i];
        double average = 0.0;
        const Status status = student_average(student, &average);

        if (status != STATUS_OK) {
            return status;
        }

        if (fprintf(trace, "%s %s | group: %s | average: %.6f\n", student->surname, student->name,
                    student->group, average)
            < 0) {
            return STATUS_IO_ERROR;
        }
    }

    return fputc('\n', trace) == EOF || fflush(trace) == EOF ? STATUS_IO_ERROR : STATUS_OK;
}

static Status write_above_report(FILE *const trace, const StudentMatches *const matches,
                                 const double average) {
    if (trace == NULL || matches == NULL || (matches->items == NULL && matches->count != 0U)
        || !isfinite(average)) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (fprintf(trace, "Above overall average: %.6f\n", average) < 0) {
        return STATUS_IO_ERROR;
    }

    if (matches->count == 0U && fputs("No students above the overall average.\n", trace) == EOF) {
        return STATUS_IO_ERROR;
    }

    for (size_t i = 0; i < matches->count; ++i) {
        if (fprintf(trace, "%s %s\n", matches->items[i]->surname, matches->items[i]->name) < 0) {
            return STATUS_IO_ERROR;
        }
    }

    return fputc('\n', trace) == EOF || fflush(trace) == EOF ? STATUS_IO_ERROR : STATUS_OK;
}

static Status distinct_paths(const char *const input, const char *const trace) {
    if (input == NULL || trace == NULL || *input == '\0' || *trace == '\0') {
        return STATUS_INVALID_ARGUMENT;
    }

    if (strcmp(input, trace) == 0) {
        return STATUS_PATH_CONFLICT;
    }

    struct stat source;
    struct stat destination;

    if (stat(input, &source) != 0) {
        return STATUS_IO_ERROR;
    }

    if (stat(trace, &destination) == 0 && source.st_dev == destination.st_dev
        && source.st_ino == destination.st_ino) {
        return STATUS_PATH_CONFLICT;
    }

    return STATUS_OK;
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

static Status read_number(const char *const prompt, const unsigned int maximum,
                          unsigned int *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *line = NULL;
    size_t capacity = 0;
    Status status = read_prompt(prompt, &line, &capacity);

    if (status == STATUS_OK) {
        status = parse_uint(line, maximum, result);
    }

    free(line);

    return status;
}

static Status run_command(Student *const students, const size_t count, FILE *const trace,
                          const unsigned int command, const double epsilon) {
    if (trace == NULL || command < 1U || command > 5U) {
        return STATUS_INVALID_ARGUMENT;
    }

    StudentMatches matches = {0};
    Status status = STATUS_OK;

    if (command == 1U) {
        if (count == 0U && puts("No students.") == EOF) {
            status = STATUS_IO_ERROR;
        }

        for (size_t i = 0; i < count && status == STATUS_OK; ++i) {
            status = print_student(stdout, &students[i]);
        }
    } else if (command == 2U || command == 3U) {
        unsigned int field = 0;

        status = read_number("Field (1 ID, 2 surname, 3 name, 4 group): ", 4U, &field);

        if (status == STATUS_OK && field == 0U) {
            status = STATUS_OUT_OF_RANGE;
        }

        if (status == STATUS_OK && command == 3U) {
            const StudentComparator comparators[] = {
                compare_students_by_id,
                compare_students_by_surname,
                compare_students_by_name,
                compare_students_by_group
            };

            status = sort_students(students, count, comparators[field - 1U]);

            if (status == STATUS_OK && puts("Sorted in ascending order.") == EOF) {
                status = STATUS_IO_ERROR;
            }
        } else if (status == STATUS_OK) {
            char *query = NULL;
            size_t capacity = 0;
            unsigned int id = 0;

            status = read_prompt("Search value: ", &query, &capacity);

            if (status == STATUS_OK && field == 1U) {
                status = parse_uint(query, UINT_MAX, &id);
            }

            if (status == STATUS_OK) {
                status =
                    find_students(students, count, (StudentField)(field - 1U), id, query, &matches);
            }

            if (status == STATUS_OK && matches.count == 0U
                && puts("No matching students.") == EOF) {
                status = STATUS_IO_ERROR;
            }

            for (size_t i = 0; i < matches.count && status == STATUS_OK; ++i) {
                status = print_student(stdout, matches.items[i]);
            }

            free(query);
        }
    } else if (command == 4U) {
        unsigned int id = 0;

        status = read_number("Student ID: ", UINT_MAX, &id);

        if (status == STATUS_OK) {
            status = find_students(students, count, BY_ID, id, NULL, &matches);
        }

        if (status == STATUS_OK) {
            status = write_id_report(trace, id, &matches);
        }
    } else {
        double average = 0.0;

        status = select_above_average(students, count, epsilon, &matches, &average);

        if (status == STATUS_OK) {
            status = write_above_report(trace, &matches, average);
        }
    }

    matches_destroy(&matches);

    if (status == STATUS_OK && (command == 4U || command == 5U)
        && puts("Report appended to trace file.") == EOF) {
        status = STATUS_IO_ERROR;
    }

    return status;
}

static Status print_error(const Status status) {
    const char *message = NULL;

    switch (status) {
        case STATUS_OK:
            return STATUS_OK;

        case STATUS_INVALID_ARGUMENT:
            message = "invalid arguments or search value";
            break;

        case STATUS_INVALID_DATA:
            message = "expected id name surname group and five unsigned-char grades; check names "
                      "and quotes";
            break;

        case STATUS_OUT_OF_RANGE:
            message = "number or allocation size is out of range";
            break;

        case STATUS_ALLOCATION_ERROR:
            message = "memory allocation failed";
            break;

        case STATUS_OPEN_ERROR:
            message = "cannot open input or trace file";
            break;

        case STATUS_IO_ERROR:
            message = "input/output operation failed";
            break;

        case STATUS_END_OF_INPUT:
            message = "input ended";
            break;

        case STATUS_EMPTY_COLLECTION:
            message = "no students; overall average is undefined";
            break;

        case STATUS_PATH_CONFLICT:
            message = "input and trace paths must refer to different files";
            break;

        default:
            message = "unknown status";
            break;
    }

    return fprintf(stderr, "Error: %s.\n", message) < 0 ? STATUS_IO_ERROR : STATUS_OK;
}

int main(const int argc, char *argv[]) {
    if (argc != 3) {
        fputs("Usage: task05 input_file trace_file\n", stderr);

        return 1;
    }

    Student *students = NULL;
    size_t count = 0;
    Status status = load_students(argv[1], &students, &count);

    if (status == STATUS_OK) {
        status = distinct_paths(argv[1], argv[2]);
    }

    FILE *trace = NULL;

    if (status == STATUS_OK) {
        trace = fopen(argv[2], "a");

        if (trace == NULL) {
            status = STATUS_OPEN_ERROR;
        }
    }

    const double epsilon = 1e-9;

    while (status == STATUS_OK) {
        if (puts("\n1 List | 2 Find | 3 Sort | 4 Report by ID | 5 Report above average | 0 Quit")
            == EOF) {
            status = STATUS_IO_ERROR;
            break;
        }

        unsigned int command = 0;
        Status action = read_number("Command: ", 5U, &command);

        if (action == STATUS_END_OF_INPUT || (action == STATUS_OK && command == 0U)) {
            break;
        }

        if (action == STATUS_OK) {
            action = run_command(students, count, trace, command, epsilon);
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

    if (trace != NULL && fclose(trace) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    students_destroy(&students, &count);

    if (fflush(stdout) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        print_error(status);

        return 1;
    }

    return 0;
}
