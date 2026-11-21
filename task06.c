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
    STATUS_INVALID_DATA,
    STATUS_OUT_OF_RANGE,
    STATUS_ALLOCATION_ERROR,
    STATUS_OPEN_ERROR,
    STATUS_IO_ERROR,
    STATUS_END_OF_INPUT,
    STATUS_EMPTY_COLLECTION,
    STATUS_DUPLICATE_EVENT
} Status;

typedef enum {
    STOP_START,
    STOP_INTERMEDIATE,
    STOP_FINISH
} StopKind;

typedef struct Stop {
    double x;
    double y;
    char arrival_text[20];
    char departure_text[20];
    char station_key[96];
    long long arrival;
    long long departure;
    StopKind kind;

    struct Stop *next;
} Stop;

typedef struct Route {
    char *vehicle;
    Stop *stops;

    struct Route *next;
} Route;

typedef struct VehicleStats {
    const char *vehicle;
    size_t route_count;
    long double total_distance;
    long double shortest_route;
    long double longest_route;
    long long shortest_stop;
    long long longest_stop;
    long long total_idle;

    struct VehicleStats *next;
} VehicleStats;

typedef enum {
    METRIC_ROUTE_COUNT,
    METRIC_TOTAL_DISTANCE,
    METRIC_LONGEST_ROUTE,
    METRIC_SHORTEST_ROUTE,
    METRIC_LONGEST_STOP,
    METRIC_SHORTEST_STOP,
    METRIC_TOTAL_IDLE
} Metric;

static Status validate_vehicle(const char *const text) {
    if (text == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (!isspace((unsigned char)*cursor)) {
            return STATUS_OK;
        }
    }

    return STATUS_INVALID_DATA;
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

Status parse_datetime(const char *const text, long long *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (strlen(text) != 19U || text[2] != '.' || text[5] != '.' || text[10] != ' '
        || text[13] != ':' || text[16] != ':') {
        return STATUS_INVALID_DATA;
    }

    for (size_t i = 0; i < 19U; ++i) {
        if (i != 2U && i != 5U && i != 10U && i != 13U && i != 16U
            && !isdigit((unsigned char)text[i])) {
            return STATUS_INVALID_DATA;
        }
    }

    const int day = (text[0] - '0') * 10 + text[1] - '0';
    const int month = (text[3] - '0') * 10 + text[4] - '0';
    const int year =
        (text[6] - '0') * 1000 + (text[7] - '0') * 100 + (text[8] - '0') * 10 + text[9] - '0';
    const int hour = (text[11] - '0') * 10 + text[12] - '0';
    const int minute = (text[14] - '0') * 10 + text[15] - '0';
    const int second = (text[17] - '0') * 10 + text[18] - '0';

    if (year == 0 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59
        || second > 59) {
        return STATUS_INVALID_DATA;
    }

    const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int leap = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);

    if (day > month_days[month - 1] + (month == 2 && leap)) {
        return STATUS_INVALID_DATA;
    }

    const long long previous_year = (long long)year - 1;

    long long days =
        previous_year * 365 + previous_year / 4 - previous_year / 100 + previous_year / 400;

    for (int i = 1; i < month; ++i) {
        days += month_days[i - 1] + (i == 2 && leap);
    }

    days += day - 1;
    *result = ((days * 24 + hour) * 60 + minute) * 60 + second;

    return STATUS_OK;
}

Status routes_destroy(Route **const routes) {
    if (routes == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (*routes != NULL) {
        Route *const route = *routes;

        *routes = route->next;

        while (route->stops != NULL) {
            Stop *const stop = route->stops;

            route->stops = stop->next;
            free(stop);
        }

        free(route->vehicle);
        free(route);
    }

    return STATUS_OK;
}

Status statistics_destroy(VehicleStats **const statistics) {
    if (statistics == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    while (*statistics != NULL) {
        VehicleStats *const current = *statistics;

        *statistics = current->next;
        free(current);
    }

    return STATUS_OK;
}

static Status stop_order(const Stop *const first, const Stop *const second, int *const result) {
    if (first == NULL || second == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (first->arrival != second->arrival) {
        *result = (first->arrival > second->arrival) - (first->arrival < second->arrival);
    } else if (first->departure != second->departure) {
        *result = (first->departure > second->departure) - (first->departure < second->departure);
    } else {
        const int order = strcmp(first->station_key, second->station_key);

        *result = order != 0 ? order : (first->kind > second->kind) - (first->kind < second->kind);
    }

    return STATUS_OK;
}

static Status insert_event(Route **const vehicles, const char *const vehicle, Stop *const stop) {
    if (vehicles == NULL || vehicle == NULL || stop == NULL || stop->next != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Route **position = vehicles;

    while (*position != NULL && strcmp((*position)->vehicle, vehicle) < 0) {
        position = &(*position)->next;
    }

    if (*position == NULL || strcmp((*position)->vehicle, vehicle) != 0) {
        Route *const created = malloc(sizeof(*created));

        if (created == NULL) {
            return STATUS_ALLOCATION_ERROR;
        }

        *created = (Route){NULL, stop, *position};
        const Status status = duplicate_text(vehicle, &created->vehicle);

        if (status != STATUS_OK) {
            free(created);

            return status;
        }

        *position = created;

        return STATUS_OK;
    }

    Stop **place = &(*position)->stops;

    while (*place != NULL) {
        int order = 0;
        const Status status = stop_order(*place, stop, &order);

        if (status != STATUS_OK) {
            return status;
        }

        if (order == 0) {
            return STATUS_DUPLICATE_EVENT;
        }

        if (order > 0) {
            break;
        }

        place = &(*place)->next;
    }

    stop->next = *place;
    *place = stop;

    return STATUS_OK;
}

static Status order_vehicle_stops(Route *const vehicle) {
    if (vehicle == NULL || vehicle->stops == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Stop *remaining = vehicle->stops;
    Stop *ordered = NULL;
    Stop *previous = NULL;
    Stop **tail = &ordered;
    int open = 0;
    Status status = STATUS_OK;

    while (remaining != NULL) {
        const long long arrival = remaining->arrival;
        Stop **choice = NULL;
        const StopKind kinds[] = {open ? STOP_INTERMEDIATE : STOP_START, STOP_FINISH};

        for (size_t i = 0; i < (open ? 2U : 1U) && choice == NULL; ++i) {
            for (Stop **candidate = &remaining;
                 *candidate != NULL && (*candidate)->arrival == arrival;
                 candidate = &(*candidate)->next) {
                if ((*candidate)->departure == arrival && (*candidate)->kind == kinds[i]) {
                    choice = candidate;
                    break;
                }
            }
        }

        if (choice == NULL) {
            if (remaining->next != NULL && remaining->next->arrival == arrival) {
                status = STATUS_INVALID_DATA;
                break;
            }

            choice = &remaining;
        }

        Stop *const current = *choice;

        if ((!open && current->kind != STOP_START) || (open && current->kind == STOP_START)
            || (previous != NULL && previous->departure > current->arrival)) {
            status = STATUS_INVALID_DATA;
            break;
        }

        *choice = current->next;
        current->next = NULL;
        *tail = current;
        tail = &current->next;
        previous = current;
        open = current->kind != STOP_FINISH;
    }

    *tail = remaining;
    vehicle->stops = ordered;

    return status == STATUS_OK && open ? STATUS_INVALID_DATA : status;
}

static Status split_routes(Route *const vehicles, Route **const result) {
    if (result == NULL || *result != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    Route *routes = NULL;
    Route **tail = &routes;
    Status status = STATUS_OK;

    for (Route *vehicle = vehicles; vehicle != NULL && status == STATUS_OK;
         vehicle = vehicle->next) {
        while (vehicle->stops != NULL && status == STATUS_OK) {
            Stop *last = vehicle->stops;

            while (last != NULL && last->kind != STOP_FINISH) {
                last = last->next;
            }

            if (last == NULL) {
                status = STATUS_INVALID_DATA;
                break;
            }

            Route *const route = malloc(sizeof(*route));

            if (route == NULL) {
                status = STATUS_ALLOCATION_ERROR;
                break;
            }

            *route = (Route){0};
            status = duplicate_text(vehicle->vehicle, &route->vehicle);

            if (status != STATUS_OK) {
                free(route);
                break;
            }

            route->stops = vehicle->stops;
            vehicle->stops = last->next;
            last->next = NULL;
            *tail = route;
            tail = &route->next;
        }
    }

    if (status != STATUS_OK) {
        routes_destroy(&routes);
    } else {
        *result = routes;
    }

    return status;
}

Status route_distance(const Route *const route, long double *const result) {
    if (route == NULL || route->stops == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    *result = 0.0L;
    const Stop *previous = route->stops;

    for (const Stop *stop = previous->next; stop != NULL; stop = stop->next) {
        const long double dx = (long double)stop->x - (long double)previous->x;
        const long double dy = (long double)stop->y - (long double)previous->y;
        const long double distance = hypotl(dx, dy);

        if (!isfinite(distance) || !isfinite(*result + distance)) {
            return STATUS_OUT_OF_RANGE;
        }

        *result += distance;
        previous = stop;
    }

    return STATUS_OK;
}

Status build_statistics(const Route *const routes, VehicleStats **const result) {
    if (result == NULL || *result != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    VehicleStats *statistics = NULL;
    Status status = STATUS_OK;

    for (const Route *route = routes; route != NULL && status == STATUS_OK; route = route->next) {
        long double distance = 0.0L;

        if (route->vehicle == NULL) {
            status = STATUS_INVALID_ARGUMENT;
            break;
        }

        status = route_distance(route, &distance);

        if (status != STATUS_OK) {
            break;
        }

        VehicleStats **place = &statistics;

        while (*place != NULL && strcmp((*place)->vehicle, route->vehicle) < 0) {
            place = &(*place)->next;
        }

        if (*place == NULL || strcmp((*place)->vehicle, route->vehicle) != 0) {
            VehicleStats *const created = malloc(sizeof(*created));

            if (created == NULL) {
                status = STATUS_ALLOCATION_ERROR;
                break;
            }

            *created = (VehicleStats){
                route->vehicle,
                0U,
                0.0L,
                distance,
                distance,
                LLONG_MAX,
                0,
                0,
                *place
            };

            *place = created;
        }

        VehicleStats *const current = *place;

        if (current->route_count == SIZE_MAX) {
            status = STATUS_OUT_OF_RANGE;
            break;
        }

        ++current->route_count;
        current->total_distance += distance;
        current->shortest_route = fminl(current->shortest_route, distance);
        current->longest_route = fmaxl(current->longest_route, distance);

        if (!isfinite(current->total_distance)) {
            status = STATUS_OUT_OF_RANGE;
            break;
        }

        for (const Stop *stop = route->stops; stop != NULL; stop = stop->next) {
            if (stop->arrival < 0 || stop->departure < stop->arrival) {
                status = STATUS_INVALID_DATA;
                break;
            }

            const long long dwell = stop->departure - stop->arrival;

            if (dwell > LLONG_MAX - current->total_idle) {
                status = STATUS_OUT_OF_RANGE;
                break;
            }

            current->total_idle += dwell;

            if (dwell < current->shortest_stop) {
                current->shortest_stop = dwell;
            }

            if (dwell > current->longest_stop) {
                current->longest_stop = dwell;
            }
        }
    }

    if (status != STATUS_OK) {
        statistics_destroy(&statistics);
    } else {
        *result = statistics;
    }

    return status;
}

Status select_vehicle(const VehicleStats *const statistics, const Metric metric, const int maximize,
                      const long double epsilon, const VehicleStats **const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    *result = NULL;

    if (metric < METRIC_ROUTE_COUNT || metric > METRIC_TOTAL_IDLE
        || (maximize != 0 && maximize != 1) || !isfinite(epsilon) || signbit(epsilon)
        || fpclassify(epsilon) == FP_ZERO) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (statistics == NULL) {
        return STATUS_EMPTY_COLLECTION;
    }

    const VehicleStats *best = statistics;

    for (const VehicleStats *current = statistics; current != NULL; current = current->next) {
        if (current->vehicle == NULL || current->route_count == 0U) {
            return STATUS_INVALID_DATA;
        }

        int order = 0;

        if (metric == METRIC_ROUTE_COUNT) {
            order = (current->route_count > best->route_count)
                - (current->route_count < best->route_count);
        } else if (metric >= METRIC_TOTAL_DISTANCE && metric <= METRIC_SHORTEST_ROUTE) {
            const long double a =
                metric == METRIC_TOTAL_DISTANCE ? current->total_distance
                : metric == METRIC_LONGEST_ROUTE ? current->longest_route
                : current->shortest_route;
            const long double b =
                metric == METRIC_TOTAL_DISTANCE ? best->total_distance
                : metric == METRIC_LONGEST_ROUTE ? best->longest_route
                : best->shortest_route;

            if (!isfinite(a) || !isfinite(b)) {
                return STATUS_INVALID_DATA;
            }

            const long double difference = a - b;

            order = difference > epsilon ? 1
                : difference < -epsilon ? -1
                : 0;
        } else {
            const long long a =
                metric == METRIC_LONGEST_STOP ? current->longest_stop
                : metric == METRIC_SHORTEST_STOP ? current->shortest_stop
                : current->total_idle;
            const long long b =
                metric == METRIC_LONGEST_STOP ? best->longest_stop
                : metric == METRIC_SHORTEST_STOP ? best->shortest_stop
                : best->total_idle;

            if (a < 0 || b < 0) {
                return STATUS_INVALID_DATA;
            }

            order = (a > b) - (a < b);
        }

        if ((maximize && order > 0) || (!maximize && order < 0)) {
            best = current;
        }
    }

    *result = best;

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

static Status parse_coordinate(const char *const text, double *const result) {
    if (text == NULL || result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (!isdigit((unsigned char)*cursor) && strchr("+-.eE", *cursor) == NULL) {
            return STATUS_INVALID_DATA;
        }
    }

    char *end = NULL;
    const double value = strtod(text, &end);

    if (end == text || *end != '\0') {
        return STATUS_INVALID_DATA;
    }

    if (!isfinite(value)) {
        return STATUS_OUT_OF_RANGE;
    }

    *result = value;

    return STATUS_OK;
}

static Status parse_event(char *const line, const double x, const double y,
                          Route **const vehicles) {
    if (line == NULL || vehicles == NULL || !isfinite(x) || !isfinite(y)) {
        return STATUS_INVALID_ARGUMENT;
    }

    char *fields[6];
    Stop data = {0};
    Status status = split_fields(line, fields, 6U);

    if (status == STATUS_OK) {
        status = validate_vehicle(fields[0]);
    }

    if (status == STATUS_OK
        && (snprintf(data.arrival_text, sizeof(data.arrival_text), "%s %s", fields[1], fields[2])
                != 19
            || snprintf(data.departure_text, sizeof(data.departure_text), "%s %s", fields[3],
                        fields[4])
                != 19)) {
        status = STATUS_INVALID_DATA;
    }

    if (status == STATUS_OK) {
        status = parse_datetime(data.arrival_text, &data.arrival);
    }

    if (status == STATUS_OK) {
        status = parse_datetime(data.departure_text, &data.departure);
    }

    if (status == STATUS_OK
        && (data.departure < data.arrival || strlen(fields[5]) != 1U
            || strchr("SIF", fields[5][0]) == NULL)) {
        status = STATUS_INVALID_DATA;
    }

    if (status != STATUS_OK) {
        return status;
    }

    data.kind = fields[5][0] == 'S' ? STOP_START
        : fields[5][0] == 'I' ? STOP_INTERMEDIATE
        : STOP_FINISH;

    data.x = x;
    data.y = y;
    const int length = snprintf(data.station_key, sizeof(data.station_key), "%a,%a", x, y);

    if (length < 0 || (size_t)length >= sizeof(data.station_key)) {
        return STATUS_OUT_OF_RANGE;
    }

    Stop *const stop = malloc(sizeof(*stop));

    if (stop == NULL) {
        return STATUS_ALLOCATION_ERROR;
    }

    *stop = data;
    status = insert_event(vehicles, fields[0], stop);

    if (status != STATUS_OK) {
        free(stop);
    }

    return status;
}

static Status load_one_file(const char *const path, Route **const vehicles) {
    if (path == NULL || *path == '\0' || vehicles == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    FILE *const input = fopen(path, "r");

    if (input == NULL) {
        return STATUS_OPEN_ERROR;
    }

    char *line = NULL;
    size_t capacity = 0;
    int header = 0;
    double x = 0.0;
    double y = 0.0;
    Status status = STATUS_OK;

    while (status == STATUS_OK) {
        status = read_line(input, &line, &capacity);

        if (status == STATUS_END_OF_INPUT) {
            status = header ? STATUS_OK : STATUS_INVALID_DATA;
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

        if (!header) {
            char *fields[2];

            status = split_fields(line, fields, 2U);

            if (status == STATUS_OK) {
                status = parse_coordinate(fields[0], &x);
            }

            if (status == STATUS_OK) {
                status = parse_coordinate(fields[1], &y);
            }

            header = status == STATUS_OK;
        } else {
            status = parse_event(line, x, y, vehicles);
        }
    }

    free(line);

    if (fclose(input) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    return status;
}

Status load_routes(const char *const paths[], const size_t path_count, Route **const result) {
    if (paths == NULL || path_count == 0U || result == NULL || *result != NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (path_count > SIZE_MAX / sizeof(*paths)) {
        return STATUS_OUT_OF_RANGE;
    }

    for (size_t i = 0; i < path_count; ++i) {
        if (paths[i] == NULL || *paths[i] == '\0') {
            return STATUS_INVALID_ARGUMENT;
        }
    }

    Route *vehicles = NULL;
    Status status = STATUS_OK;

    for (size_t i = 0; i < path_count && status == STATUS_OK; ++i) {
        status = load_one_file(paths[i], &vehicles);
    }

    for (Route *vehicle = vehicles; vehicle != NULL && status == STATUS_OK;
         vehicle = vehicle->next) {
        status = order_vehicle_stops(vehicle);
    }

    if (status == STATUS_OK) {
        status = split_routes(vehicles, result);
    }

    routes_destroy(&vehicles);

    return status;
}

static Status print_routes(FILE *const output, const Route *const routes) {
    if (output == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (routes == NULL && fputs("No routes.\n", output) == EOF) {
        return STATUS_IO_ERROR;
    }

    const char kinds[] = "SIF";

    for (const Route *route = routes; route != NULL; route = route->next) {
        if (fprintf(output, "\nRoute vehicle=%s\n", route->vehicle) < 0) {
            return STATUS_IO_ERROR;
        }

        for (const Stop *stop = route->stops; stop != NULL; stop = stop->next) {
            if (fprintf(output, "%c (%.17g, %.17g) %s -> %s\n", kinds[stop->kind], stop->x, stop->y,
                        stop->arrival_text, stop->departure_text)
                < 0) {
                return STATUS_IO_ERROR;
            }
        }
    }

    return STATUS_OK;
}

static Status print_statistics(FILE *const output, const VehicleStats *const statistics) {
    if (output == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (statistics == NULL) {
        return fputs("No vehicles.\n", output) == EOF ? STATUS_IO_ERROR : STATUS_OK;
    }

    for (const VehicleStats *current = statistics; current != NULL; current = current->next) {
        if (fprintf(output,
                    "Vehicle: %s | routes: %zu | total distance: %.6Lf | shortest route: %.6Lf | "
                    "longest route: %.6Lf | shortest stop: %lld s | longest stop: %lld s | idle: "
                    "%lld s\n",
                    current->vehicle, current->route_count, current->total_distance,
                    current->shortest_route, current->longest_route, current->shortest_stop,
                    current->longest_stop, current->total_idle)
            < 0) {
            return STATUS_IO_ERROR;
        }
    }

    return STATUS_OK;
}

static Status print_selection(FILE *const output, const VehicleStats *const vehicle,
                              const Metric metric) {
    if (output == NULL || vehicle == NULL || metric < METRIC_ROUTE_COUNT
        || metric > METRIC_TOTAL_IDLE) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (fprintf(output, "Vehicle: %s\n", vehicle->vehicle) < 0) {
        return STATUS_IO_ERROR;
    }

    int written = 0;

    if (metric == METRIC_ROUTE_COUNT) {
        written = fprintf(output, "Value: %zu routes\n", vehicle->route_count);
    } else if (metric >= METRIC_TOTAL_DISTANCE && metric <= METRIC_SHORTEST_ROUTE) {
        const long double distance =
            metric == METRIC_TOTAL_DISTANCE ? vehicle->total_distance
            : metric == METRIC_LONGEST_ROUTE ? vehicle->longest_route
            : vehicle->shortest_route;

        written = fprintf(output, "Value: %.6Lf distance units\n", distance);
    } else {
        const long long seconds =
            metric == METRIC_LONGEST_STOP ? vehicle->longest_stop
            : metric == METRIC_SHORTEST_STOP ? vehicle->shortest_stop
            : vehicle->total_idle;

        written = fprintf(output, "Value: %lld seconds\n", seconds);
    }

    return written < 0 ? STATUS_IO_ERROR : STATUS_OK;
}

static Status read_command(unsigned int *const result) {
    if (result == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }

    if (fputs("Command: ", stdout) == EOF || fflush(stdout) == EOF) {
        return STATUS_IO_ERROR;
    }

    char *line = NULL;
    size_t capacity = 0;
    Status status = read_line(stdin, &line, &capacity);
    unsigned int value = 0;

    if (status == STATUS_OK && *line == '\0') {
        status = STATUS_INVALID_DATA;
    }

    for (const char *cursor = line; status == STATUS_OK && *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            status = STATUS_INVALID_DATA;
        } else {
            const unsigned int digit = (unsigned int)(*cursor - '0');

            if (value > (UINT_MAX - digit) / 10U) {
                status = STATUS_OUT_OF_RANGE;
            } else {
                value = value * 10U + digit;
            }
        }
    }

    if (status == STATUS_OK && value > 11U) {
        status = STATUS_OUT_OF_RANGE;
    }

    free(line);

    if (status == STATUS_OK) {
        *result = value;
    }

    return status;
}

static Status print_error(const Status status) {
    const char *message = NULL;

    switch (status) {
        case STATUS_OK:
            return STATUS_OK;

        case STATUS_INVALID_ARGUMENT:
            message = "invalid arguments, vehicle number or epsilon";
            break;

        case STATUS_INVALID_DATA:
            message = "invalid data, calendar date, overlapping stops or S/I/F route sequence";
            break;

        case STATUS_OUT_OF_RANGE:
            message = "number, distance or allocation size is out of range";
            break;

        case STATUS_ALLOCATION_ERROR:
            message = "memory allocation failed";
            break;

        case STATUS_OPEN_ERROR:
            message = "cannot open an input file";
            break;

        case STATUS_IO_ERROR:
            message = "input/output operation failed";
            break;

        case STATUS_END_OF_INPUT:
            message = "input ended";
            break;

        case STATUS_EMPTY_COLLECTION:
            message = "no completed routes";
            break;

        case STATUS_DUPLICATE_EVENT:
            message = "duplicate stop record";
            break;

        default:
            message = "unknown status";
            break;
    }

    return fprintf(stderr, "Error: %s.\n", message) < 0 ? STATUS_IO_ERROR : STATUS_OK;
}

int main(const int argc, char *argv[]) {
    if (argc < 2) {
        fputs("Usage: task06 stop_file [stop_file ...]\n", stderr);

        return 1;
    }

    Route *routes = NULL;
    VehicleStats *statistics = NULL;
    Status status = load_routes((const char *const *)(argv + 1), (size_t)(argc - 1), &routes);

    if (status == STATUS_OK) {
        status = build_statistics(routes, &statistics);
    }

    const long double epsilon = 1e-9L;

    while (status == STATUS_OK) {
        if (puts(
                "\n1 Most routes | 2 Fewest routes | 3 Longest total path | 4 Shortest total path\n"
                "5 Longest route | 6 Shortest route | 7 Longest stop | 8 Shortest stop\n"
                "9 Most idle time | 10 Show routes | 11 Summary | 0 Quit")
            == EOF) {
            status = STATUS_IO_ERROR;
            break;
        }

        unsigned int command = 0;
        Status action = read_command(&command);

        if (action == STATUS_END_OF_INPUT || (action == STATUS_OK && command == 0U)) {
            break;
        }

        if (action == STATUS_OK && command <= 9U) {
            const Metric metrics[] = {
                METRIC_ROUTE_COUNT,
                METRIC_ROUTE_COUNT,
                METRIC_TOTAL_DISTANCE,
                METRIC_TOTAL_DISTANCE,
                METRIC_LONGEST_ROUTE,
                METRIC_SHORTEST_ROUTE,
                METRIC_LONGEST_STOP,
                METRIC_SHORTEST_STOP,
                METRIC_TOTAL_IDLE
            };

            const int maximize[] = {1, 0, 1, 0, 1, 0, 1, 0, 1};
            const VehicleStats *selected = NULL;

            action = select_vehicle(statistics, metrics[command - 1U], maximize[command - 1U],
                                    epsilon, &selected);

            if (action == STATUS_OK) {
                action = print_selection(stdout, selected, metrics[command - 1U]);
            }
        } else if (action == STATUS_OK && command == 10U) {
            action = print_routes(stdout, routes);
        } else if (action == STATUS_OK) {
            action = print_statistics(stdout, statistics);
        }

        if (action != STATUS_OK) {
            if (print_error(action) != STATUS_OK) {
                status = STATUS_IO_ERROR;
            } else if (action == STATUS_ALLOCATION_ERROR || action == STATUS_IO_ERROR) {
                status = action;
            }
        }
    }

    statistics_destroy(&statistics);
    routes_destroy(&routes);

    if (fflush(stdout) == EOF && status == STATUS_OK) {
        status = STATUS_IO_ERROR;
    }

    if (status != STATUS_OK) {
        print_error(status);

        return 1;
    }

    return 0;
}
