#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CHAR_BIT != 8
#error This program requires eight-bit bytes.
#endif

enum { BYTE_VALUES = 256, CODE_CAPACITY = 256 };

typedef enum {
    STATUS_OK,
    STATUS_INVALID_ARGUMENT,
    STATUS_INVALID_DATA,
    STATUS_OUT_OF_RANGE,
    STATUS_ALLOCATION_ERROR,
    STATUS_OPEN_ERROR,
    STATUS_IO_ERROR
} Status;

typedef struct Node {
    unsigned long long weight;
    int byte;
    const struct Node *left;
    const struct Node *right;
} Node;

typedef struct {
    const Node *items[BYTE_VALUES];
    size_t size;
} MinHeap;

typedef struct {
    char bits[CODE_CAPACITY];
    size_t length;
} HuffmanCode;

typedef struct {
    HuffmanCode codes[BYTE_VALUES];
    unsigned long long original_bytes;
    unsigned long long encoded_bits;
    unsigned long long encoded_bytes;
    long double savings_percent;
} HuffmanResult;

static Status heap_push(MinHeap *const heap, const Node *const node) {
    if (heap == NULL || node == NULL || node->weight == 0ULL
        || heap->size > BYTE_VALUES) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (heap->size == BYTE_VALUES) {
        return STATUS_OUT_OF_RANGE;
    }
    size_t position = heap->size;
    ++heap->size;
    while (position > 0U) {
        const size_t parent = (position - 1U) / 2U;
        if (heap->items[parent]->weight <= node->weight) {
            break;
        }
        heap->items[position] = heap->items[parent];
        position = parent;
    }
    heap->items[position] = node;
    return STATUS_OK;
}

static Status heap_pop(MinHeap *const heap, const Node **const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    *result = NULL;
    if (heap == NULL || heap->size == 0U || heap->size > BYTE_VALUES) {
        return STATUS_INVALID_ARGUMENT;
    }
    *result = heap->items[0];
    --heap->size;
    if (heap->size == 0U) {
        return STATUS_OK;
    }

    const Node *const last = heap->items[heap->size];
    size_t position = 0U;
    while (position * 2U + 1U < heap->size) {
        size_t child = position * 2U + 1U;
        if (child + 1U < heap->size
            && heap->items[child + 1U]->weight < heap->items[child]->weight) {
            ++child;
        }
        if (last->weight <= heap->items[child]->weight) {
            break;
        }
        heap->items[position] = heap->items[child];
        position = child;
    }
    heap->items[position] = last;
    return STATUS_OK;
}

static Status read_frequencies(FILE *const stream,
                               unsigned long long frequencies[BYTE_VALUES]) {
    if (stream == NULL || frequencies == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    memset(frequencies, 0, BYTE_VALUES * sizeof(*frequencies));
    unsigned long long total = 0ULL;
    unsigned char buffer[4096];
    size_t count = 0U;
    while ((count = fread(buffer, 1U, sizeof(buffer), stream)) != 0U) {
        if ((unsigned long long)count > ULLONG_MAX - total) {
            return STATUS_OUT_OF_RANGE;
        }
        total += (unsigned long long)count;
        for (size_t i = 0U; i < count; ++i) {
            ++frequencies[buffer[i]];
        }
    }
    return ferror(stream) ? STATUS_IO_ERROR : STATUS_OK;
}

static Status build_tree(const unsigned long long frequencies[BYTE_VALUES],
                          Node **const storage, const Node **const root) {
    if (frequencies == NULL || storage == NULL || root == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    *storage = NULL;
    *root = NULL;
    size_t distinct = 0U;
    for (size_t byte = 0U; byte < BYTE_VALUES; ++byte) {
        if (frequencies[byte] != 0ULL) {
            ++distinct;
        }
    }
    if (distinct == 0U) {
        return STATUS_OK;
    }

    Node *const nodes = malloc((2U * distinct - 1U) * sizeof(*nodes));
    if (nodes == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }
    MinHeap heap = {{NULL}, 0U};
    size_t used = 0U;
    Status status = STATUS_OK;
    for (size_t byte = 0U; byte < BYTE_VALUES && status == STATUS_OK; ++byte) {
        if (frequencies[byte] != 0ULL) {
            nodes[used] = (Node){frequencies[byte], (int)byte, NULL, NULL};
            status = heap_push(&heap, &nodes[used]);
            ++used;
        }
    }
    while (heap.size > 1U && status == STATUS_OK) {
        const Node *left = NULL;
        const Node *right = NULL;
        status = heap_pop(&heap, &left);
        if (status == STATUS_OK) {
            status = heap_pop(&heap, &right);
        }
        if (status == STATUS_OK && left->weight > ULLONG_MAX - right->weight) {
            status = STATUS_OUT_OF_RANGE;
        }
        if (status == STATUS_OK) {
            nodes[used] = (Node){left->weight + right->weight, -1, left, right};
            status = heap_push(&heap, &nodes[used]);
            ++used;
        }
    }
    if (status == STATUS_OK) {
        status = heap_pop(&heap, root);
    }
    if (status != STATUS_OK) {
        free(nodes);
        return status;
    }
    *storage = nodes;
    return STATUS_OK;
}

static Status fill_codes(const Node *const node, char *const path,
                          const size_t depth, HuffmanCode codes[BYTE_VALUES]) {
    if (node == NULL || path == NULL || codes == NULL || depth >= CODE_CAPACITY) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (node->weight == 0ULL) {
        return STATUS_INVALID_DATA;
    }
    if (node->byte >= 0) {
        if (node->byte >= BYTE_VALUES || node->left != NULL || node->right != NULL) {
            return STATUS_INVALID_DATA;
        }
        const size_t length = depth == 0U ? 1U : depth;
        if (depth == 0U) {
            path[0] = '0';
        }
        memcpy(codes[node->byte].bits, path, length);
        codes[node->byte].bits[length] = '\0';
        codes[node->byte].length = length;
        return STATUS_OK;
    }
    if (node->byte != -1 || node->left == NULL || node->right == NULL
        || depth >= CODE_CAPACITY - 1U) {
        return STATUS_INVALID_DATA;
    }
    path[depth] = '0';
    Status status = fill_codes(node->left, path, depth + 1U, codes);
    if (status == STATUS_OK) {
        path[depth] = '1';
        status = fill_codes(node->right, path, depth + 1U, codes);
    }
    return status;
}

Status analyze_frequencies(const unsigned long long frequencies[BYTE_VALUES],
                            HuffmanResult *const result) {
    if (frequencies == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    for (size_t byte = 0U; byte < BYTE_VALUES; ++byte) {
        if (frequencies[byte] > ULLONG_MAX - result->original_bytes) {
            return STATUS_OUT_OF_RANGE;
        }
        result->original_bytes += frequencies[byte];
    }
    if (result->original_bytes == 0ULL) {
        return STATUS_OK;
    }

    Node *storage = NULL;
    const Node *root = NULL;
    Status status = build_tree(frequencies, &storage, &root);
    if (status == STATUS_OK) {
        char path[CODE_CAPACITY];
        status = fill_codes(root, path, 0U, result->codes);
    }
    free(storage);
    if (status != STATUS_OK) {
        return status;
    }

    for (size_t byte = 0U; byte < BYTE_VALUES; ++byte) {
        if (frequencies[byte] != 0ULL) {
            const unsigned long long length = (unsigned long long)result->codes[byte].length;
            if (length == 0ULL) {
                return STATUS_INVALID_DATA;
            }
            if (frequencies[byte] > (ULLONG_MAX - result->encoded_bits) / length) {
                return STATUS_OUT_OF_RANGE;
            }
            result->encoded_bits += frequencies[byte] * length;
        }
    }
    result->encoded_bytes = result->encoded_bits / 8ULL
                         + (result->encoded_bits % 8ULL != 0ULL ? 1ULL : 0ULL);
    result->savings_percent = 100.0L * (1.0L - (long double)result->encoded_bytes
                                              / (long double)result->original_bytes);
    return STATUS_OK;
}

static Status print_report(const unsigned long long frequencies[BYTE_VALUES],
                            const HuffmanResult *const result) {
    if (frequencies == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (puts("Byte Frequency Code") == EOF) {
        return STATUS_IO_ERROR;
    }
    for (size_t byte = 0U; byte < BYTE_VALUES; ++byte) {
        if (frequencies[byte] != 0ULL
            && printf("%zu %llu %s\n", byte, frequencies[byte], result->codes[byte].bits) < 0) {
            return STATUS_IO_ERROR;
        }
    }
    if (printf("Original size (bytes): %llu\nEncoded size (bits): %llu\n"
               "Encoded size (bytes): %llu\nSavings: %.2Lf%%\n",
               result->original_bytes, result->encoded_bits,
               result->encoded_bytes, result->savings_percent) < 0) {
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

int main(const int argc, char *argv[]) {
    if (argc != 2 || argv[1][0] == '\0') {
        fputs("Usage: task03 path_to_file\n", stderr);
        return 1;
    }
    FILE *const input = fopen(argv[1], "rb");
    Status status = input == NULL ? STATUS_OPEN_ERROR : STATUS_OK;
    unsigned long long frequencies[BYTE_VALUES];
    if (status == STATUS_OK) {
        status = read_frequencies(input, frequencies);
    }
    if (input != NULL && fclose(input) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    HuffmanResult result;
    if (status == STATUS_OK) {
        status = analyze_frequencies(frequencies, &result);
    }
    if (status == STATUS_OK) {
        status = print_report(frequencies, &result);
    }
    if (status == STATUS_OK && fflush(stdout) == EOF) {
        status = STATUS_IO_ERROR;
    }
    switch (status) {
        case STATUS_OK:
            return 0;
        case STATUS_INVALID_ARGUMENT:
            fputs("Error: invalid function arguments.\n", stderr);
            break;
        case STATUS_INVALID_DATA:
            fputs("Error: invalid Huffman tree or code data.\n", stderr);
            break;
        case STATUS_OUT_OF_RANGE:
            fputs("Error: file size, frequencies or encoded size exceed unsigned long long.\n", stderr);
            break;
        case STATUS_ALLOCATION_ERROR:
            fputs("Error: memory allocation failed.\n", stderr);
            break;
        case STATUS_OPEN_ERROR:
            fputs("Error: cannot open the input file.\n", stderr);
            break;
        case STATUS_IO_ERROR:
            fputs("Error: input/output operation failed.\n", stderr);
            break;
        default:
            fputs("Error: unknown status.\n", stderr);
            break;
    }
    return 1;
}
