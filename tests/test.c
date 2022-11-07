#include "../huffman_code.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

int main()
{
    uint8_t lengths[288];

    // lengths for block type 01
    //ref: https://www.ietf.org/rfc/rfc1951.txt section 3.2.6
    for (uint8_t i = 0; i < 144; ++i)
        lengths[i] = 8;
    for (uint16_t i = 144; i < 256; ++i)
        lengths[i] = 9;
    for (uint16_t i = 256; i < 280; ++i)
        lengths[i] = 7;
    for (uint16_t i = 280; i <= 287; ++i)
        lengths[i] = 8;

    struct huffman codes[288];

    bool success = generate_huffman_codes(lengths, codes, 288, 15);

    if (!success) {
        fprintf(stderr, "Expected generate_huffman_codes to succeed\n");
        return 1;
    }

    if (strcmp(codes[0].huffman_code, "00110000") != 0) {
        fprintf(stderr, "code for byte 0 didn't match\n");
        return 1;
    }

    if (strcmp(codes[143].huffman_code, "10111111") != 0) {
        fprintf(stderr, "code for byte 143 didn't match\n");
        return 1;
    }

    if (strcmp(codes[144].huffman_code, "110010000") != 0) {
        fprintf(stderr, "code for byte 144 didn't match\n");
        return 1;
    }

    if (strcmp(codes[255].huffman_code, "111111111") != 0) {
        fprintf(stderr, "code for byte 255 didn't match\n");
        return 1;
    }

    if (strcmp(codes[256].huffman_code, "0000000") != 0) {
        fprintf(stderr, "code for byte 256 didn't match\n");
        return 1;
    }

    if (strcmp(codes[279].huffman_code, "0010111") != 0) {
        fprintf(stderr, "code for byte 279 didn't match\n");
        return 1;
    }

    if (strcmp(codes[280].huffman_code, "11000000") != 0) {
        fprintf(stderr, "code for byte 280 didn't match\n");
        return 1;
    }

    if (strcmp(codes[287].huffman_code, "11000111") != 0) {
        fprintf(stderr, "code for byte 287 didn't match\n");
        return 1;
    }

    printf("All tests passed\n");
    return 0;
}
