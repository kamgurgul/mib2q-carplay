/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AA_TRIP_H
#define AA_TRIP_H
#include <stdint.h>

#ifndef AA_TRIP_PATH
#define AA_TRIP_PATH "/tmp/aa_trip"
#endif
#define AA_TRIP_RECORD_SIZE 320
#define AA_TRIP_DEST_SIZE 256

typedef struct {
    char destination[AA_TRIP_DEST_SIZE];
    int32_t meters, seconds;             /* -1 = not supplied */
    uint64_t state_ms, position_ms;      /* system clock; NOT the navigation UTC clock */
} aa_trip_t;

/* Called before navxlate overwrites the original message. Does not change route ownership. */
void aa_trip_update(unsigned id, const unsigned char *body, unsigned size);
void aa_trip_reset(void);
#endif
