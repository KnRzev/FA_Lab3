#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_INVALID_EXPRESSION,
    STATUS_OUT_OF_RANGE,
    STATUS_ALLOCATION_ERROR,
    STATUS_OPEN_ERROR,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT
} Status;

typedef struct TreeNode {
    char *name;

    struct TreeNode *parent;
    struct TreeNode *child;
    struct TreeNode *next;
} TreeNode;

typedef enum {
    EXPECT_NODE,
    EXPECT_FIRST_CHILD,
    AFTER_NODE
} ParseState;

Status tree_destroy(TreeNode **const root) {
    if (root == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    TreeNode *node = *root;

    while (node != NULL) {
        if (node->child != NULL) {
            TreeNode *const child = node->child;

            node->child = child->next;
            node = child;
        } else {
            TreeNode *const parent = node->parent;

            free(node->name);
            free(node);
            node = parent;
        }
    }

    *root = NULL;

    return STATUS_OK;
}

static Status create_node(const char *const name, const size_t length, TreeNode *const parent,
                          TreeNode **const result) {
    if (name == NULL || length == 0 || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (length == SIZE_MAX) {
        return STATUS_OUT_OF_RANGE;
    }

    TreeNode *const node = malloc(sizeof(*node));

    if (node == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    node->name = malloc(length + 1);

    if (node->name == NULL) {
        free(node);

        return STATUS_ALLOCATION_ERROR;
    }

    memcpy(node->name, name, length);
    node->name[length] = '\0';
    node->parent = parent;
    node->child = NULL;
    node->next = NULL;
    *result = node;

    return STATUS_OK;
}

Status tree_parse(const char *const expression, TreeNode **const result) {
    if (expression == NULL || result == NULL || *result != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const char *position = expression;
    TreeNode *root = NULL;
    TreeNode *parent = NULL;
    TreeNode *last = NULL;
    Status status = STATUS_OK;
    ParseState state = EXPECT_NODE;
    int can_open = 0;

    while (status == STATUS_OK) {
        while (isspace((unsigned char)*position)) {
            ++position;
        }

        if (*position == '\0') {
            if (state != AFTER_NODE || parent != NULL) {
                status = STATUS_INVALID_EXPRESSION;
            }

            break;
        }

        if (state != AFTER_NODE) {
            if (*position == ')' && state == EXPECT_FIRST_CHILD) {
                last = parent;
                parent = parent->parent;
                state = AFTER_NODE;
                can_open = 0;
                ++position;
                continue;
            }

            const char *const start = position;

            while (*position != '\0' && !isspace((unsigned char)*position) && *position != '('
                   && *position != ')' && *position != ',') {
                ++position;
            }

            if (position == start) {
                status = STATUS_INVALID_EXPRESSION;
                break;
            }

            TreeNode *node = NULL;

            status = create_node(start, (size_t)(position - start), parent, &node);

            if (status != STATUS_OK) {
                break;
            }

            if (root == NULL) {
                root = node;
            } else if (parent->child == NULL) {
                parent->child = node;
            } else {
                last->next = node;
            }

            last = node;
            state = AFTER_NODE;
            can_open = 1;
        } else if (*position == '(' && can_open) {
            parent = last;
            last = NULL;
            state = EXPECT_FIRST_CHILD;
            can_open = 0;
            ++position;
        } else if (*position == ',' && parent != NULL) {
            state = EXPECT_NODE;
            can_open = 0;
            ++position;
        } else if (*position == ')' && parent != NULL) {
            last = parent;
            parent = parent->parent;
            can_open = 0;
            ++position;
        } else {
            status = STATUS_INVALID_EXPRESSION;
        }
    }

    if (status != STATUS_OK) {
        tree_destroy(&root);
    } else {
        *result = root;
    }

    return status;
}

Status tree_print(const TreeNode *const root, FILE *const output) {
    if (root == NULL || output == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const TreeNode *node = root;
    size_t depth = 0;

    while (node != NULL) {
        for (size_t i = 0; i < depth; ++i) {
            if (fputs("    ", output) == EOF) {
                return STATUS_IO_ERROR;
            }
        }

        if (fprintf(output, "%s\n", node->name) < 0) {
            return STATUS_IO_ERROR;
        }

        if (node->child != NULL) {
            node = node->child;
            ++depth;
        } else {
            while (node != root && node->next == NULL) {
                node = node->parent;
                --depth;
            }

            if (node == root) {
                break;
            }

            node = node->next;
        }
    }

    return STATUS_OK;
}

static Status read_line(FILE *const input, char **const result) {
    if (input == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    size_t length = 0;
    size_t capacity = 128;
    char *line = malloc(capacity);
    Status status = STATUS_OK;
    int character = 0;

    *result = NULL;

    if (line == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    while ((character = fgetc(input)) != EOF && character != '\n') {
        if (character == 0) {
            status = STATUS_INVALID_EXPRESSION;
            break;
        }

        if (length == capacity - 1) {
            if (capacity > SIZE_MAX / 2) {
                status = STATUS_OUT_OF_RANGE;
                break;
            }

            const size_t new_capacity = capacity * 2;
            char *const resized = realloc(line, new_capacity);

            if (resized == NULL) {
                status = STATUS_ALLOCATION_ERROR;
                break;
            }

            line = resized;
            capacity = new_capacity;
        }

        line[length] = (char)character;
        ++length;
    }

    if (ferror(input)) {
        status = STATUS_IO_ERROR;
    }

    if (status == STATUS_OK && character == EOF && length == 0) {
        status = STATUS_END_OF_INPUT;
    }

    if (status != STATUS_OK) {
        free(line);
    } else {
        line[length] = '\0';
        *result = line;
    }

    return status;
}

static Status process_file(FILE *const input, FILE *const output, size_t *const error_line) {
    if (input == NULL || output == NULL || error_line == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    size_t line_number = 0;
    size_t tree_number = 0;
    Status status = STATUS_OK;

    *error_line = 0;

    while (status == STATUS_OK) {
        char *line = NULL;
        TreeNode *root = NULL;

        status = read_line(input, &line);

        if (status == STATUS_END_OF_INPUT) {
            status = STATUS_OK;
            break;
        }

        if (line_number == SIZE_MAX) {
            status = STATUS_OUT_OF_RANGE;
        } else {
            ++line_number;
        }

        if (status == STATUS_OK) {
            const char *start = line;

            while (isspace((unsigned char)*start)) {
                ++start;
            }

            if (*start != '\0') {
                status = tree_parse(start, &root);

                if (status == STATUS_OK) {
                    ++tree_number;

                    if (fprintf(output, "Tree %zu:\n", tree_number) < 0) {
                        status = STATUS_IO_ERROR;
                    }
                }

                if (status == STATUS_OK) {
                    status = tree_print(root, output);
                }

                if (status == STATUS_OK && fputc('\n', output) == EOF) {
                    status = STATUS_IO_ERROR;
                }
            }
        }

        tree_destroy(&root);
        free(line);
    }

    if (status != STATUS_OK) {
        *error_line = line_number;
    }

    return status;
}

static Status print_error(const Status status, const size_t line_number) {
    const char *message = NULL;

    switch (status) {
        case STATUS_INVALID_ARGUMENT: {
            message = "Invalid argument.";
            break;
        }

        case STATUS_INVALID_EXPRESSION: {
            message = "Invalid bracket expression.";
            break;
        }

        case STATUS_OUT_OF_RANGE: {
            message = "Input is too large.";
            break;
        }

        case STATUS_ALLOCATION_ERROR: {
            message = "Memory allocation failed.";
            break;
        }

        case STATUS_OPEN_ERROR: {
            message = "Cannot open file.";
            break;
        }

        case STATUS_IO_ERROR: {
            message = "Input/output error.";
            break;
        }

        default: {
            return STATUS_INVALID_ARGUMENT;
        }
    }

    if (line_number != 0 && fprintf(stderr, "Line %zu: ", line_number) < 0) {
        return STATUS_IO_ERROR;
    }

    return fprintf(stderr, "%s\n", message) < 0 ? STATUS_IO_ERROR : STATUS_OK;
}

int main(const int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);

        return 1;
    }

    if (argv[1][0] == '\0' || argv[2][0] == '\0') {
        print_error(STATUS_INVALID_ARGUMENT, 0);

        return 1;
    }

    struct stat input_info;
    struct stat output_info;

    if (strcmp(argv[1], argv[2]) == 0
        || (stat(argv[1], &input_info) == 0 && stat(argv[2], &output_info) == 0
            && input_info.st_dev == output_info.st_dev
            && input_info.st_ino == output_info.st_ino)) {
        fprintf(stderr, "Input and output must be different files.\n");

        return 1;
    }

    FILE *const input = fopen(argv[1], "rb");

    if (input == NULL) {
        print_error(STATUS_OPEN_ERROR, 0);

        return 1;
    }

    FILE *const output = fopen(argv[2], "wb");

    if (output == NULL) {
        const Status status = fclose(input) == 0 ? STATUS_OPEN_ERROR : STATUS_IO_ERROR;

        print_error(status, 0);

        return 1;
    }

    size_t error_line = 0;
    Status status = process_file(input, output, &error_line);

    if (fclose(input) != 0) {
        status = STATUS_IO_ERROR;
    }

    if (fclose(output) != 0) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        print_error(status, error_line);

        return 1;
    }

    return 0;
}
