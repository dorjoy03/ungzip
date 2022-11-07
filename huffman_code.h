#ifndef HUFFMAN_CODE
#define HUFFMAN_CODE

#include <inttypes.h>
#include <stdbool.h>

struct huffman {
    uint8_t huffman_code[16]; // either '1' or '0'
    uint8_t len; // let's be explicit about length
};

bool generate_huffman_codes(uint8_t *code_lengths, struct huffman *codes,
                            uint16_t length, uint8_t limit);

#endif
