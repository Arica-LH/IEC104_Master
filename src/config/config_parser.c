#include "config/config_parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *config_trim(char *value)
{
    char *end;

    while (isspace((unsigned char)*value)) {
        ++value;
    }

    if (*value == '\0') {
        return value;
    }

    end = value + strlen(value) - 1;
    while (end > value && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }

    return value;
}

int config_parse_bool(const char *value, int *out)
{
    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcmp(value, "1") == 0 ||
        strcasecmp(value, "on") == 0) {
        *out = 1;
        return 0;
    }
    if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcmp(value, "0") == 0 ||
        strcasecmp(value, "off") == 0) {
        *out = 0;
        return 0;
    }

    return -1;
}

int config_parse_int_range(const char *value, int min_value, int max_value, int *out)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *config_trim(end) != '\0' || parsed < min_value || parsed > max_value) {
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

int config_parse_double_min(const char *value, double min_value, double *out)
{
    char *end = NULL;
    double parsed;

    errno = 0;
    parsed = strtod(value, &end);
    if (errno != 0 || end == value || *config_trim(end) != '\0' || parsed < min_value) {
        return -1;
    }

    *out = parsed;
    return 0;
}

int config_parse_debug_level(const char *value, app_debug_level_t *out)
{
    if (strcasecmp(value, "off") == 0 || strcasecmp(value, "none") == 0 ||
        strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out = APP_DEBUG_OFF;
        return 0;
    }

    if (strcasecmp(value, "info") == 0 || strcasecmp(value, "brief") == 0 ||
        strcasecmp(value, "normal") == 0 || strcmp(value, "1") == 0) {
        *out = APP_DEBUG_INFO;
        return 0;
    }

    if (strcasecmp(value, "detail") == 0 || strcasecmp(value, "debug") == 0 ||
        strcasecmp(value, "verbose") == 0 || strcmp(value, "2") == 0) {
        *out = APP_DEBUG_DETAIL;
        return 0;
    }

    return -1;
}

void config_set_string(char *dest, size_t dest_size, const char *value)
{
    snprintf(dest, dest_size, "%s", value);
}
