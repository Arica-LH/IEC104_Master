#include "point_store.h"

#include "logging.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum point_type {
    POINT_TYPE_YX = 1,
    POINT_TYPE_YC,
    POINT_TYPE_ENERGY
} point_type_t;

typedef struct point_entry {
    point_type_t type;
    uint16_t common_address;
    uint32_t ioa;
    int initialized;
    int yx_value;
    double numeric_value;
    uint8_t quality;
    time_t updated_at;
    struct point_entry *next;
} point_entry_t;

struct point_store {
    point_entry_t *buckets[1024];
};

static unsigned int point_hash(point_type_t type, uint16_t common_address, uint32_t ioa)
{
    uint32_t value = ((uint32_t)type * 2654435761u) ^ ((uint32_t)common_address << 16) ^ ioa;
    value ^= value >> 16;
    return value % 1024u;
}

static point_entry_t *get_or_create(point_store_t *store,
                                    point_type_t type,
                                    uint16_t common_address,
                                    uint32_t ioa)
{
    unsigned int bucket = point_hash(type, common_address, ioa);
    point_entry_t *entry = store->buckets[bucket];

    while (entry != NULL) {
        if (entry->type == type && entry->common_address == common_address && entry->ioa == ioa) {
            return entry;
        }
        entry = entry->next;
    }

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        log_write(LOG_LEVEL_ERROR, "failed to allocate point state ca=%u ioa=%u", common_address, ioa);
        return NULL;
    }

    entry->type = type;
    entry->common_address = common_address;
    entry->ioa = ioa;
    entry->next = store->buckets[bucket];
    store->buckets[bucket] = entry;
    return entry;
}

point_store_t *point_store_create(void)
{
    return calloc(1, sizeof(point_store_t));
}

void point_store_destroy(point_store_t *store)
{
    if (store == NULL) {
        return;
    }

    for (size_t i = 0; i < sizeof(store->buckets) / sizeof(store->buckets[0]); ++i) {
        point_entry_t *entry = store->buckets[i];
        while (entry != NULL) {
            point_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
    }

    free(store);
}

void point_store_update_yx(point_store_t *store,
                           uint16_t common_address,
                           uint32_t ioa,
                           int value,
                           uint8_t quality,
                           int log_unchanged)
{
    point_entry_t *entry;
    int changed;

    if (store == NULL) {
        return;
    }

    entry = get_or_create(store, POINT_TYPE_YX, common_address, ioa);
    if (entry == NULL) {
        return;
    }

    changed = !entry->initialized || entry->yx_value != value || entry->quality != quality;
    if (changed || log_unchanged) {
        log_write(changed ? LOG_LEVEL_INFO : LOG_LEVEL_DEBUG,
                  "YX ca=%u ioa=%u value=%d quality=0x%02x%s",
                  common_address,
                  ioa,
                  value,
                  quality,
                  changed ? " changed" : "");
    }

    entry->initialized = 1;
    entry->yx_value = value;
    entry->quality = quality;
    entry->updated_at = time(NULL);
}

void point_store_update_yc(point_store_t *store,
                           uint16_t common_address,
                           uint32_t ioa,
                           double value,
                           uint8_t quality,
                           int log_all)
{
    point_entry_t *entry;
    int changed;

    if (store == NULL) {
        return;
    }

    entry = get_or_create(store, POINT_TYPE_YC, common_address, ioa);
    if (entry == NULL) {
        return;
    }

    changed = !entry->initialized || fabs(entry->numeric_value - value) > 0.000001 || entry->quality != quality;
    if (changed || log_all) {
        log_write(changed ? LOG_LEVEL_INFO : LOG_LEVEL_DEBUG,
                  "YC ca=%u ioa=%u value=%.6f quality=0x%02x%s",
                  common_address,
                  ioa,
                  value,
                  quality,
                  changed ? " changed" : "");
    }

    entry->initialized = 1;
    entry->numeric_value = value;
    entry->quality = quality;
    entry->updated_at = time(NULL);
}

void point_store_update_energy(point_store_t *store,
                               uint16_t common_address,
                               uint32_t ioa,
                               double value,
                               uint8_t flags,
                               double abs_threshold,
                               double rate_threshold)
{
    point_entry_t *entry;
    double delta = 0.0;
    double previous = 0.0;
    int spike = 0;

    if (store == NULL) {
        return;
    }

    entry = get_or_create(store, POINT_TYPE_ENERGY, common_address, ioa);
    if (entry == NULL) {
        return;
    }

    if (entry->initialized) {
        previous = entry->numeric_value;
        delta = value - previous;

        if (fabs(delta) >= abs_threshold) {
            spike = 1;
        }
        if (previous > 0.000001 && value / previous >= rate_threshold) {
            spike = 1;
        }
        if (previous > 0.000001 && value >= 0.0 && previous / (value + 0.000001) >= rate_threshold) {
            spike = 1;
        }
    }

    if (spike) {
        log_write(LOG_LEVEL_WARN,
                  "ENERGY_SPIKE ca=%u ioa=%u previous=%.3f current=%.3f delta=%.3f flags=0x%02x "
                  "threshold_abs=%.3f threshold_rate=%.3f",
                  common_address,
                  ioa,
                  previous,
                  value,
                  delta,
                  flags,
                  abs_threshold,
                  rate_threshold);
    } else {
        log_write(LOG_LEVEL_INFO,
                  "ENERGY ca=%u ioa=%u value=%.3f delta=%.3f flags=0x%02x",
                  common_address,
                  ioa,
                  value,
                  entry->initialized ? delta : 0.0,
                  flags);
    }

    entry->initialized = 1;
    entry->numeric_value = value;
    entry->quality = flags;
    entry->updated_at = time(NULL);
}
