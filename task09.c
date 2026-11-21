#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_INVALID_DATA,
    STATUS_OUT_OF_RANGE,
    STATUS_ALLOCATION_ERROR,
    STATUS_OPEN_ERROR,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT,
    STATUS_EMPTY_TREE
} Status;

typedef struct WordNode {
    char *word;
    size_t count;

    struct WordNode *left;
    struct WordNode *right;
} WordNode;

typedef struct {
    WordNode *root;
    size_t size;
} WordTree;

Status tree_destroy(WordTree *const tree) {
    if (tree == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (tree->root != NULL) {
        WordNode *const node = tree->root;

        if (node->left != NULL) {
            WordNode *const left = node->left;

            node->left = left->right;
            left->right = node;
            tree->root = left;
        } else {
            tree->root = node->right;
            free(node->word);
            free(node);
        }
    }

    tree->size = 0;

    return STATUS_OK;
}

Status tree_insert(WordTree *const tree, const char *const word, const size_t count) {
    if (tree == NULL || word == NULL || word[0] == '\0' || count == 0) {
        return STATUS_INVALID_ARGUMENT;
    }

    WordNode **link = &tree->root;

    while (*link != NULL) {
        const int comparison = strcmp(word, (*link)->word);

        if (comparison == 0) {
            if ((*link)->count > SIZE_MAX - count) {
                return STATUS_OUT_OF_RANGE;
            }

            (*link)->count += count;

            return STATUS_OK;
        }

        if (comparison < 0) {
            link = &(*link)->left;
        } else {
            link = &(*link)->right;
        }
    }

    const size_t length = strlen(word);

    if (tree->size == SIZE_MAX || length == SIZE_MAX) {
        return STATUS_OUT_OF_RANGE;
    }

    WordNode *const node = malloc(sizeof(*node));

    if (node == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    node->word = malloc(length + 1);

    if (node->word == NULL) {
        free(node);

        return STATUS_ALLOCATION_ERROR;
    }

    memcpy(node->word, word, length + 1);
    node->count = count;
    node->left = NULL;
    node->right = NULL;
    *link = node;
    ++tree->size;

    return STATUS_OK;
}

Status tree_find(const WordTree *const tree, const char *const word, size_t *const count) {
    if (tree == NULL || word == NULL || word[0] == '\0' || count == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const WordNode *node = tree->root;

    *count = 0;

    while (node != NULL) {
        const int comparison = strcmp(word, node->word);

        if (comparison == 0) {
            *count = node->count;

            return STATUS_OK;
        }

        if (comparison < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }

    return STATUS_OK;
}

static Status collect_nodes(const WordTree *const tree, const WordNode ***const result,
                            size_t *const depth) {
    if (tree == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    *result = NULL;

    if (depth != NULL) {
        *depth = 0;
    }

    if ((tree->root == NULL) != (tree->size == 0)) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (tree->size == 0) {
        return STATUS_OK;
    }

    if (tree->size > SIZE_MAX / sizeof(const WordNode *)) {
        return STATUS_OUT_OF_RANGE;
    }

    const WordNode **const nodes = malloc(tree->size * sizeof(*nodes));

    if (nodes == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    size_t first = 0;
    size_t count = 1;
    size_t levels = 0;
    Status status = STATUS_OK;

    nodes[0] = tree->root;

    while (first < count && status == STATUS_OK) {
        const size_t level_end = count;

        ++levels;

        while (first < level_end && status == STATUS_OK) {
            const WordNode *const node = nodes[first];
            const WordNode *const children[2] = {node->left, node->right};

            ++first;

            for (size_t i = 0; i < 2; ++i) {
                if (children[i] != NULL) {
                    if (count == tree->size) {
                        status = STATUS_INVALID_ARGUMENT;
                        break;
                    }

                    nodes[count] = children[i];
                    ++count;
                }
            }
        }
    }

    if (status == STATUS_OK && count != tree->size) {
        status = STATUS_INVALID_ARGUMENT;
    }

    if (status != STATUS_OK) {
        free(nodes);
    } else {
        *result = nodes;

        if (depth != NULL) {
            *depth = levels;
        }
    }

    return status;
}

static int compare_frequency(const void *const first, const void *const second) {
    const WordNode *const a = *(const WordNode *const *)first;
    const WordNode *const b = *(const WordNode *const *)second;

    if (a->count > b->count) {
        return -1;
    }

    if (a->count < b->count) {
        return 1;
    }

    return strcmp(a->word, b->word);
}

Status tree_top(const WordTree *const tree, const size_t requested, const WordNode ***const result,
                size_t *const count) {
    if (tree == NULL || requested == 0 || result == NULL || count == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    *count = 0;

    const Status status = collect_nodes(tree, result, NULL);

    if (status != STATUS_OK) {
        return status;
    }

    if (tree->size > 1) {
        qsort(*result, tree->size, sizeof(**result), compare_frequency);
    }

    *count = requested < tree->size ? requested : tree->size;

    return STATUS_OK;
}

Status tree_extremes(const WordTree *const tree, const WordNode **const shortest,
                     const WordNode **const longest) {
    if (tree == NULL || shortest == NULL || longest == NULL || shortest == longest) {
        return STATUS_INVALID_ARGUMENT;
    }

    const WordNode **nodes = NULL;
    const Status status = collect_nodes(tree, &nodes, NULL);

    *shortest = NULL;
    *longest = NULL;

    if (status != STATUS_OK) {
        return status;
    }

    if (tree->size == 0) {
        return STATUS_EMPTY_TREE;
    }

    *shortest = nodes[0];
    *longest = nodes[0];

    size_t minimum = strlen(nodes[0]->word);
    size_t maximum = minimum;

    for (size_t i = 1; i < tree->size; ++i) {
        const size_t length = strlen(nodes[i]->word);

        if (length < minimum) {
            minimum = length;
            *shortest = nodes[i];
        }

        if (length > maximum) {
            maximum = length;
            *longest = nodes[i];
        }
    }

    free(nodes);

    return STATUS_OK;
}

Status tree_depth(const WordTree *const tree, size_t *const depth) {
    if (depth == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    const WordNode **nodes = NULL;
    const Status status = collect_nodes(tree, &nodes, depth);

    free(nodes);

    return status;
}

static Status append_character(char **const text, size_t *const length, size_t *const capacity,
                               const char character) {
    if (*length > SIZE_MAX - 2) {
        return STATUS_OUT_OF_RANGE;
    }

    if (*length + 2 > *capacity) {
        size_t new_capacity = *capacity == 0 ? 32 : *capacity;

        if (new_capacity <= SIZE_MAX / 2) {
            new_capacity *= 2;
        } else {
            new_capacity = *length + 2;
        }

        char *const resized = realloc(*text, new_capacity);

        if (resized == NULL) {
            return STATUS_ALLOCATION_ERROR;
        }

        *text = resized;
        *capacity = new_capacity;
    }

    (*text)[*length] = character;
    ++*length;
    (*text)[*length] = '\0';

    return STATUS_OK;
}

Status tree_read_text(const char *const path, const unsigned char *const separators,
                      WordTree *const result) {
    if (path == NULL || path[0] == '\0' || separators == NULL || result == NULL
        || result->root != NULL || result->size != 0) {
        return STATUS_INVALID_ARGUMENT;
    }

    FILE *const file = fopen(path, "rb");

    if (file == NULL) {
        return STATUS_OPEN_ERROR;
    }

    WordTree tree = {0};
    char *word = NULL;
    size_t length = 0;
    size_t capacity = 0;
    Status status = STATUS_OK;
    int character = 0;

    while (status == STATUS_OK && (character = fgetc(file)) != EOF) {
        if (character == 0) {
            status = STATUS_INVALID_DATA;
        } else if (separators[(unsigned char)character] != 0) {
            if (length != 0) {
                status = tree_insert(&tree, word, 1);
                length = 0;
            }
        } else {
            status = append_character(&word, &length, &capacity, (char)character);
        }
    }

    if (ferror(file)) {
        status = STATUS_IO_ERROR;
    }

    if (status == STATUS_OK && length != 0) {
        status = tree_insert(&tree, word, 1);
    }

    if (fclose(file) != 0) {
        status = STATUS_IO_ERROR;
    }

    free(word);

    if (status != STATUS_OK) {
        tree_destroy(&tree);
    } else {
        *result = tree;
    }

    return status;
}

Status tree_save(const WordTree *const tree, const char *const path) {
    if (tree == NULL || path == NULL || path[0] == '\0') {
        return STATUS_INVALID_ARGUMENT;
    }

    const WordNode **nodes = NULL;
    Status status = collect_nodes(tree, &nodes, NULL);

    if (status != STATUS_OK) {
        return status;
    }

    FILE *const file = fopen(path, "wb");

    if (file == NULL) {
        free(nodes);

        return STATUS_OPEN_ERROR;
    }

    if (fwrite("FA09", 1, 4, file) != 4 || fwrite(&tree->size, sizeof(tree->size), 1, file) != 1) {
        status = STATUS_IO_ERROR;
    }

    for (size_t i = 0; i < tree->size && status == STATUS_OK; ++i) {
        const size_t length = strlen(nodes[i]->word);

        if (fwrite(&length, sizeof(length), 1, file) != 1
            || fwrite(&nodes[i]->count, sizeof(nodes[i]->count), 1, file) != 1
            || fwrite(nodes[i]->word, 1, length, file) != length) {
            status = STATUS_IO_ERROR;
        }
    }

    if (fclose(file) != 0) {
        status = STATUS_IO_ERROR;
    }

    free(nodes);

    return status;
}

static Status read_bytes(FILE *const file, void *const result, const size_t size) {
    if (fread(result, 1, size, file) != size) {
        return ferror(file) ? STATUS_IO_ERROR : STATUS_INVALID_DATA;
    }

    return STATUS_OK;
}

Status tree_load(const char *const path, WordTree *const result) {
    if (path == NULL || path[0] == '\0' || result == NULL || result->root != NULL
        || result->size != 0) {
        return STATUS_INVALID_ARGUMENT;
    }

    FILE *const file = fopen(path, "rb");

    if (file == NULL) {
        return STATUS_OPEN_ERROR;
    }

    WordTree tree = {0};
    char signature[4] = {0};
    size_t count = 0;
    Status status = read_bytes(file, signature, sizeof(signature));

    if (status == STATUS_OK && memcmp(signature, "FA09", 4) != 0) {
        status = STATUS_INVALID_DATA;
    }

    if (status == STATUS_OK) {
        status = read_bytes(file, &count, sizeof(count));
    }

    if (status == STATUS_OK && count > SIZE_MAX / sizeof(WordNode *)) {
        status = STATUS_INVALID_DATA;
    }

    for (size_t i = 0; i < count && status == STATUS_OK; ++i) {
        size_t length = 0;
        size_t frequency = 0;
        char *word = NULL;

        status = read_bytes(file, &length, sizeof(length));

        if (status == STATUS_OK) {
            status = read_bytes(file, &frequency, sizeof(frequency));
        }

        if (status == STATUS_OK && (length == 0 || length == SIZE_MAX || frequency == 0)) {
            status = STATUS_INVALID_DATA;
        }

        if (status == STATUS_OK) {
            word = malloc(length + 1);

            if (word == NULL) {
                status = STATUS_ALLOCATION_ERROR;
            }
        }

        if (status == STATUS_OK) {
            status = read_bytes(file, word, length);
        }

        if (status == STATUS_OK) {
            if (memchr(word, '\0', length) != NULL) {
                status = STATUS_INVALID_DATA;
            } else {
                word[length] = '\0';
                status = tree_insert(&tree, word, frequency);

                if (status == STATUS_OK && tree.size != i + 1) {
                    status = STATUS_INVALID_DATA;
                }
            }
        }

        free(word);
    }

    if (status == STATUS_OK && fgetc(file) != EOF) {
        status = STATUS_INVALID_DATA;
    }

    if (ferror(file)) {
        status = STATUS_IO_ERROR;
    }

    if (fclose(file) != 0) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        tree_destroy(&tree);
    } else {
        *result = tree;
    }

    return status;
}

static Status read_line(char **const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *line = NULL;
    size_t length = 0;
    size_t capacity = 0;
    Status status = STATUS_OK;
    int character = 0;

    *result = NULL;

    while ((character = getchar()) != EOF && character != '\n') {
        if (character == 0) {
            status = STATUS_INVALID_DATA;
        }

        if (status == STATUS_OK) {
            status = append_character(&line, &length, &capacity, (char)character);
        }
    }

    if (ferror(stdin)) {
        status = STATUS_IO_ERROR;
    }

    if (status == STATUS_OK && character == EOF && length == 0) {
        status = STATUS_END_OF_INPUT;
    }

    if (status == STATUS_OK && length > 0 && line[length - 1] == '\r') {
        line[--length] = '\0';
    }

    if (status == STATUS_OK && line == NULL) {
        line = malloc(1);

        if (line == NULL) {
            status = STATUS_ALLOCATION_ERROR;
        } else {
            line[0] = '\0';
        }
    }

    if (status != STATUS_OK) {
        free(line);
    } else {
        *result = line;
    }

    return status;
}

static Status parse_size(const char *const text, size_t *const result) {
    if (text == NULL || text[0] == '\0' || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    size_t value = 0;

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return STATUS_INVALID_DATA;
        }

        const size_t digit = (size_t)(text[i] - '0');

        if (value > (SIZE_MAX - digit) / 10) {
            return STATUS_OUT_OF_RANGE;
        }

        value = value * 10 + digit;
    }

    *result = value;

    return STATUS_OK;
}

static Status parse_separators(const int count, char *const values[],
                               unsigned char *const separators) {
    if (count < 1 || values == NULL || separators == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    memset(separators, 0, (size_t)UCHAR_MAX + 1);

    for (int i = 0; i < count; ++i) {
        unsigned char character = 0;

        if (values[i] == NULL || values[i][0] == '\0') {
            return STATUS_INVALID_ARGUMENT;
        }

        if (values[i][1] == '\0') {
            character = (unsigned char)values[i][0];
        } else if (strcmp(values[i], "\\n") == 0) {
            character = '\n';
        } else if (strcmp(values[i], "\\t") == 0) {
            character = '\t';
        } else if (strcmp(values[i], "\\r") == 0) {
            character = '\r';
        } else {
            return STATUS_INVALID_ARGUMENT;
        }

        separators[character] = 1;
    }

    return STATUS_OK;
}

static Status print_tree(const WordTree *const tree) {
    const WordNode **nodes = NULL;
    Status status = collect_nodes(tree, &nodes, NULL);

    if (status != STATUS_OK) {
        return status;
    }

    if (tree->size == 0 && printf("No words.\n") < 0) {
        status = STATUS_IO_ERROR;
    }

    for (size_t i = 0; i < tree->size && status == STATUS_OK; ++i) {
        const WordNode *const node = nodes[i];

        if (printf("\"%s\" | count: %zu | left: %s | right: %s\n", node->word, node->count,
                   node->left == NULL ? "(none)" : node->left->word,
                   node->right == NULL ? "(none)" : node->right->word)
            < 0) {
            status = STATUS_IO_ERROR;
        }
    }

    free(nodes);

    return status;
}

static Status execute_command(WordTree *const tree, const size_t command) {
    char *input = NULL;
    Status status = STATUS_OK;

    if (command == 1 || command == 2 || command == 5 || command == 6) {
        const char *const prompt = command == 1 ? "Word: "
            : command == 2                      ? "Number of words: "
                                                : "Tree file path: ";

        if (printf("%s", prompt) < 0 || fflush(stdout) != 0) {
            return STATUS_IO_ERROR;
        }

        status = read_line(&input);

        if (status != STATUS_OK) {
            return status;
        }
    }

    switch (command) {
        case 1: {
            size_t count = 0;

            status = tree_find(tree, input, &count);

            if (status == STATUS_OK && printf("Occurrences: %zu\n", count) < 0) {
                status = STATUS_IO_ERROR;
            }

            break;
        }

        case 2: {
            size_t requested = 0;
            size_t count = 0;
            const WordNode **nodes = NULL;

            status = parse_size(input, &requested);

            if (status == STATUS_OK) {
                status = tree_top(tree, requested, &nodes, &count);
            }

            if (status == STATUS_OK && count == 0 && printf("No words.\n") < 0) {
                status = STATUS_IO_ERROR;
            }

            for (size_t i = 0; i < count && status == STATUS_OK; ++i) {
                if (printf("%s | count: %zu\n", nodes[i]->word, nodes[i]->count) < 0) {
                    status = STATUS_IO_ERROR;
                }
            }

            free(nodes);
            break;
        }

        case 3: {
            const WordNode *shortest = NULL;
            const WordNode *longest = NULL;

            status = tree_extremes(tree, &shortest, &longest);

            if (status == STATUS_OK
                && printf("Shortest: %s\nLongest: %s\n", shortest->word, longest->word) < 0) {
                status = STATUS_IO_ERROR;
            }

            break;
        }

        case 4: {
            size_t depth = 0;

            status = tree_depth(tree, &depth);

            if (status == STATUS_OK && printf("Tree depth (number of levels): %zu\n", depth) < 0) {
                status = STATUS_IO_ERROR;
            }

            break;
        }

        case 5: {
            status = tree_save(tree, input);

            if (status == STATUS_OK && printf("Tree saved.\n") < 0) {
                status = STATUS_IO_ERROR;
            }

            break;
        }

        case 6: {
            WordTree restored = {0};

            status = tree_load(input, &restored);

            if (status == STATUS_OK) {
                tree_destroy(tree);
                *tree = restored;

                if (printf("Tree restored.\n") < 0) {
                    status = STATUS_IO_ERROR;
                }
            }

            break;
        }

        case 7: {
            status = print_tree(tree);
            break;
        }

        default: {
            status = STATUS_INVALID_ARGUMENT;
            break;
        }
    }

    free(input);

    return status;
}

static Status print_error(const Status status) {
    const char *message = NULL;

    switch (status) {
        case STATUS_INVALID_ARGUMENT: {
            message = "Invalid argument or command.";
            break;
        }

        case STATUS_INVALID_DATA: {
            message = "Invalid input data or tree file.";
            break;
        }

        case STATUS_OUT_OF_RANGE: {
            message = "Value is out of range.";
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

        case STATUS_EMPTY_TREE: {
            message = "No words.";
            break;
        }

        default: {
            return STATUS_INVALID_ARGUMENT;
        }
    }

    return fprintf(stderr, "%s\n", message) < 0 ? STATUS_IO_ERROR : STATUS_OK;
}

int main(const int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <text file> <separator> [separator ...]\n", argv[0]);
        fprintf(stderr, "Each separator is one character; \\n, \\t and \\r are also accepted.\n");

        return 1;
    }

    WordTree tree = {0};
    unsigned char separators[UCHAR_MAX + 1U] = {0};
    Status status = parse_separators(argc - 2, argv + 2, separators);

    if (status == STATUS_OK) {
        status = tree_read_text(argv[1], separators, &tree);
    }

    if (status == STATUS_OK && printf("Different words: %zu\n", tree.size) < 0) {
        status = STATUS_IO_ERROR;
    }

    int finished = 0;

    while (status == STATUS_OK && !finished) {
        char *input = NULL;
        size_t command = 0;

        if (printf("\n1 Find word | 2 Top words | 3 Shortest and longest | 4 Depth\n"
                   "5 Save tree | 6 Restore tree | 7 Show tree | 0 Quit\nCommand: ")
                < 0
            || fflush(stdout) != 0) {
            status = STATUS_IO_ERROR;
            break;
        }

        status = read_line(&input);

        if (status == STATUS_OK) {
            status = parse_size(input, &command);

            if (status == STATUS_OK) {
                if (command == 0) {
                    finished = 1;
                } else {
                    status = execute_command(&tree, command);
                }
            }
        }

        free(input);

        if (status == STATUS_END_OF_INPUT) {
            status = STATUS_OK;
            finished = 1;
        } else if (status != STATUS_OK && status != STATUS_ALLOCATION_ERROR
                   && status != STATUS_IO_ERROR) {
            status = print_error(status);
        }
    }

    tree_destroy(&tree);

    if (fflush(stdout) != 0) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        print_error(status);

        return 1;
    }

    return 0;
}
