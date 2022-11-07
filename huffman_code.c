#include "huffman_code.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

static inline void get_huffman_code_string(uint16_t code, uint8_t len,
                                           uint8_t *str)
{
     for (uint8_t i = 0; i < len; ++i) {
        bool bit = (code >> (len - i - 1)) & 1;
        if (bit)
            str[i] = '1';
        else
            str[i] = '0';
    }

    str[len] = '\0';
    return;
}

// ref: https://www.rfc-editor.org/rfc/rfc1951.txt section 3.2.2
bool generate_huffman_codes(uint8_t *code_lengths, struct huffman *codes,
                            uint16_t length, uint8_t limit)
{
    if (length > 288) {
        fprintf(stderr, "Expecting length to be less than 288\n");
        return false;
    }

    uint16_t code_length_counts[16];

    for (uint8_t bits = 0; bits < 16; ++bits)
        code_length_counts[bits] = 0;

    for (uint16_t i = 0; i < length; ++i) {
        if (code_lengths[i] > limit || code_lengths[i] > 15)
            return false;
        ++code_length_counts[code_lengths[i]];
    }

    uint16_t next_code_for_length[16];

    for (uint8_t bits = 0; bits < 16; ++bits)
        next_code_for_length[bits] = 0;

    uint16_t code = 0;
    code_length_counts[0] = 0;
    for (uint8_t bits = 1; bits < 16; ++bits) {
        code = (code + code_length_counts[bits - 1]) << 1;
        next_code_for_length[bits] = code;
    }

    for (uint16_t i = 0; i < length; ++i) {
        codes[i].len = code_lengths[i];
        if (codes[i].len == 0) {
            codes[i].huffman_code[0] = '\0';
            continue;
        }
        get_huffman_code_string(next_code_for_length[codes[i].len], codes[i].len,
                                codes[i].huffman_code);
        ++next_code_for_length[codes[i].len];
    }

    return true;
}
