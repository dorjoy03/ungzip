#ifndef DECOMPRESS
#define DECOMPRESS

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

bool decompress_members(uint8_t *buf, size_t buf_len, FILE *f);

#endif
