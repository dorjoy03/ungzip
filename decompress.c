#include "decompress.h"
#include "huffman_tree.h"

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#define MAX_DISTANCE 32768
#define OUT_BUF_SIZE 8192

struct value_and_bits {
    uint16_t value;       // value of the length or distance code
    uint8_t extra_bits;   // extra bits to read after the code
};

struct decompression_data {
    uint8_t *buf;           // input buffer of compressed file
    size_t buf_len;         // input buffer length
    size_t buf_pos;         // next position in input buffer we need to read
    uint8_t byte_pos;       // next bit position in current byte of input buffer
    uint8_t *back_refs;     // last 32768 decompressed bytes (cyclic)
    uint16_t back_refs_pos; // next position in back refs we will put the next decompressed byte
    bool back_refs_filled;  // if back refs has been fully filled at least once
    uint8_t *out_buf;       // output buffer
    uint32_t out_pos;       // next position in output buffer
    FILE *f;                // output file stream
};

// {length, extra_bits} for length codes 257 to 285
// ref: https://www.ietf.org/rfc/rfc1951.txt section 3.2.5
static struct value_and_bits length_data[] = {{3, 0}, {4, 0}, {5, 0}, {6, 0},
                                              {7, 0}, {8, 0}, {9, 0}, {10, 0},
                                              {11, 1}, {13, 1}, {15, 1},
                                              {17, 1}, {19, 2}, {23, 2},
                                              {27, 2}, {31, 2}, {35, 3},
                                              {43, 3}, {51, 3}, {59, 3},
                                              {67, 4}, {83, 4}, {99, 4},
                                              {115, 4}, {131, 5}, {163, 5},
                                              {195, 5}, {227, 5}, {258, 0}};


// {distance, extra_bits} for distance codes 0 to 29
// ref: https://www.ietf.org/rfc/rfc1951.txt section 3.2.5
static struct value_and_bits dist_data[] = {{1, 0}, {2, 0}, {3, 0}, {4, 0},
                                            {5, 1}, {7, 1}, {9, 2}, {13, 2},
                                            {17, 3}, {25, 3}, {33, 4}, {49, 4},
                                            {65, 5}, {97, 5}, {129, 6},
                                            {193, 6}, {257, 7}, {385, 7},
                                            {513, 8}, {769, 8}, {1025, 9},
                                            {1537, 9}, {2049, 10}, {3073, 10},
                                            {4097, 11}, {6145, 11}, {8193, 12},
                                            {12289, 12}, {16385, 13},
                                            {24577, 13}};


// return false if invalid member header
static bool check_member_header(uint8_t *buf, size_t buf_len, size_t *buf_pos)
{
    size_t pos = *buf_pos;

    if (pos >= buf_len || buf_len - pos < 10) {
        fprintf(stderr, "Unexpected buffer length. Expecting at least 10 bytes "
                "for member header\n");
        return false;
    }

    uint8_t ID1 = buf[pos++];
    if (ID1 != 0x1f) {
        fprintf(stderr, "Invalid ID1 byte\n");
        return false;
    }

    uint8_t ID2 = buf[pos++];
    if (ID2 != 0x8b) {
        fprintf(stderr, "Invalid ID2 byte\n");
        return false;
    }

    uint8_t CM = buf[pos++];
    if (CM != 8) {
        fprintf(stderr, "Unknown compression method\n");
        return false;
    }

    uint8_t FLG = buf[pos++];

    bool FTEXT    = FLG & 0x01u;   // 0x01 = 0000 0001
    bool FHCRC    = FLG & 0x02u;   // 0x02 = 0000 0010
    bool FEXTRA   = FLG & 0x04u;   // 0x04 = 0000 0100
    bool FNAME    = FLG & 0x08u;   // 0x08 = 0000 1000
    bool FCOMMENT = FLG & 0x10u;   // 0x10 = 0001 0000

    bool RESERVED_BIT_5 = FLG & 0x20u;  // 0x20 = 0010 0000
    bool RESERVED_BIT_6 = FLG & 0x40u;  // 0x40 = 0100 0000
    bool RESERVED_BIT_7 = FLG & 0x80u;  // 0x80 = 1000 0000

    // to be compliant we need to return error
    // if reserved bits are set to non-zero
    if (RESERVED_BIT_5 || RESERVED_BIT_6 || RESERVED_BIT_7) {
        fprintf(stderr, "Reserved bits should be set to zero\n");
        return false;
    }

    // multi-byte numbers are stored with the least significant byte first
    uint32_t MTIME = buf[pos] + 256 * buf[pos + 1] + 65536 * buf[pos + 2] +
        16777216 * buf[pos + 3];
    pos += 4;

    uint8_t XFL = buf[pos++];
    uint8_t OS = buf[pos++];

    uint16_t XLEN = 0;
    if (FEXTRA) {
        if (pos >= buf_len || buf_len - pos < 2) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        XLEN = buf[pos] + 256 * buf[pos + 1];
        pos += 2;
        if (pos >= buf_len || buf_len - pos < XLEN) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        pos += XLEN;
    }

    //original file name, zero-terminated
    if (FNAME) {
        if (pos >= buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        while (buf[pos++]) {
            if (pos >= buf_len) {
                fprintf(stderr, "Unexpected buffer length\n");
                return false;
            }
        }
    }

    // file comment, zero-terminated
    if (FCOMMENT) {
        if (pos >= buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        while (buf[pos++]) {
            if (pos >= buf_len) {
                fprintf(stderr, "Unexpected buffer length\n");
                return false;
            }
        }
    }

    uint16_t CRC16 = 0;
    if (FHCRC) {
        if (pos >= buf_len || buf_len - pos < 2) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        CRC16 = buf[pos] + 256 * buf[pos + 1];
        pos += 2;
    }

    *buf_pos = pos;
    return true;
}

// return false if invalid member trailer
static bool check_member_trailer(uint8_t *buf, size_t buf_len, size_t *buf_pos)
{
    size_t pos = *buf_pos;

    if (pos >= buf_len || buf_len - pos < 8) {
        fprintf(stderr, "Unexpected buffer length. Expecting 8 bytes "
                "after compressed blocks for CRC32 and ISIZE\n");
        return false;
    }

    // we don't yet check for error using the CRC values which isn't a
    // requirement to be compliant

    // multi-byte numbers are stored with the least significant byte first
    uint32_t CRC_32 = buf[pos] + 256 * buf[pos + 1] + 65536 * buf[pos + 2] +
        16777216 * buf[pos + 3];
    pos += 4;
    uint32_t ISIZE = buf[pos] + 256 * buf[pos + 1] + 65536 * buf[pos + 2] +
        16777216 * buf[pos + 3];
    pos += 4;

    *buf_pos = pos;
    return true;
}

static inline void increment_bit_position(struct decompression_data *data)
{
    if (data->byte_pos == 7) {
        data->byte_pos = 0;
        data->buf_pos++;
        return;
    }

    data->byte_pos++;
}

static inline bool is_length_code(int16_t code)
{
    return code >= 257 && code <= 285;
}

static inline bool is_literal_code(int16_t code)
{
    return code >= 0 && code <= 255;
}

static inline bool is_literal_length_code(int16_t code)
{
    return code >= 0 && code <= 285;
}

static inline bool is_distance_code(int16_t code)
{
    return code >= 0 && code <= 29;
}

static inline bool is_code_length_code(int16_t code)
{
    return code >= 0 && code <= 18;
}

static bool handle_literal_codes(struct decompression_data *data,
                                 uint8_t *codes, uint16_t len)
{
    for (uint16_t i = 0; i < len; ++i) {
        if (data->out_pos == OUT_BUF_SIZE) {
            if (fwrite(data->out_buf, 1, data->out_pos, data->f) !=
                data->out_pos) {
                fprintf(stderr, "Could not write full buffer\n");
                return false;
            }
            data->out_pos = 0;
        }
        data->out_buf[data->out_pos++] = codes[i];
        data->back_refs[data->back_refs_pos] = codes[i];
        data->back_refs_pos = (data->back_refs_pos + 1) % MAX_DISTANCE;
        if (!data->back_refs_filled && data->back_refs_pos == 0)
            data->back_refs_filled = true;
    }

    // could be that at the end of the loop we have full buffer
    if (data->out_pos == OUT_BUF_SIZE) {
        if (fwrite(data->out_buf, 1, data->out_pos, data->f) != data->out_pos) {
            fprintf(stderr, "Could not write full buffer\n");
            return false;
        }
        data->out_pos = 0;
    }

    return true;
}

static bool decompress_block_type_00(struct decompression_data *data)
{
    if (data->byte_pos != 0) {
        data->buf_pos++;
        data->byte_pos = 0;
    }

    if (data->buf_pos >= data->buf_len || data->buf_len - data->buf_pos < 4) {
        fprintf(stderr, "Unexpected buffer length\n");
        return false;
    }

    uint16_t LEN = data->buf[data->buf_pos] +
        256 * data->buf[data->buf_pos + 1];
    uint16_t NLEN = data->buf[data->buf_pos + 2] +
        256 * data->buf[data->buf_pos + 3];

    data->buf_pos += 4;

    if (LEN != (uint16_t) (~NLEN)) {
        fprintf(stderr, "LEN doesn't match ~NLEN in block type 00\n");
        return false;
    }

    if (data->buf_pos >= data->buf_len || data->buf_len - data->buf_pos < LEN) {
        fprintf(stderr, "Unexpected buffer length\n");
        return false;
    }

    bool success = handle_literal_codes(data, data->buf + data->buf_pos, LEN);
    if (!success) {
        fprintf(stderr, "Failed to handle literal codes in block type 00\n");
        return false;
    }
    data->buf_pos += LEN;

    return true;
}

// bits in order from lsb to msb
static bool read_bits(struct decompression_data *data, uint8_t bits,
                      uint16_t *bits_value)
{
    uint16_t tmp = 0;
    for (uint8_t i = 0; i < bits; ++i) {
        if (data->buf_pos >= data->buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        bool bit = (data->buf[data->buf_pos] >> data->byte_pos) & 1;
        increment_bit_position(data);
        if (bit)
            tmp |= (uint16_t) (1u << i);
    }

    *bits_value = tmp;

    return true;
}

static struct node *find_huffman_code(struct decompression_data *data,
                                      struct node *root)
{
    if (root == NULL) {
        fprintf(stderr, "Unexpected NULL node given to find huffman code\n");
        return NULL;
    }

    struct node *cur = root;
    while (cur->code == -1) {
        if (data->buf_pos >= data->buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        bool bit = (data->buf[data->buf_pos] >> data->byte_pos) & 1;
        increment_bit_position(data);
        cur = bit ? cur->right : cur->left;
        if (cur == NULL) {
            fprintf(stderr, "Unexpected NULL node trying to find "
                    "huffman code\n");
            return NULL;
        }
    }

    return cur;
}

static bool length_from_length_code(struct decompression_data *data,
                                    uint16_t code, uint16_t *length)
{
    if (!is_length_code(code)) {
        fprintf(stderr, "Expecting valid length code\n");
        return false;
    }

    uint16_t length_start = length_data[code - 257].value;
    uint8_t extra_bits = length_data[code - 257].extra_bits;
    uint16_t extra_bits_value = 0;
    bool success = read_bits(data, extra_bits, &extra_bits_value);
    if (!success) {
        fprintf(stderr, "Failed to read extra length bits\n");
        return false;
    }

    // for byte 284 extra bits of length 5 don't use the last possible
    // value 31 (11111) which would make the length 227 + 31 = 258.
    // 258 has separate length code 285
    if (code == 284 && extra_bits_value == 31) {
        fprintf(stderr, "Unexpected length extra value 31 for code 284\n");
        return false;
    }

    uint16_t len = length_start + extra_bits_value;
    if (len > 258 || len < 3) {
        fprintf(stderr, "Expecting length to be between 3 and 258\n");
        return false;
    }

    *length = len;
    return true;
}

static bool distance_from_distance_code(struct decompression_data *data,
                                        uint8_t code, uint16_t *distance)
{
    if (!is_distance_code(code)) {
        fprintf(stderr, "Expecting valid distance code\n");
        return false;
    }

    uint16_t distance_start = dist_data[code].value;
    uint8_t extra_bits = dist_data[code].extra_bits;
    uint16_t extra_bits_value = 0;
    bool success = read_bits(data, extra_bits, &extra_bits_value);
    if (!success) {
        fprintf(stderr, "Failed to read distance extra bits\n");
        return false;
    }

    uint16_t dist = distance_start + extra_bits_value;
    if (dist < 1 || dist > 32768) {
        fprintf(stderr, "Expecting distance to be between 1 and 32768\n");
        return false;
    }

    *distance = dist;
    return true;
}

static bool copy_bytes_from_distance(struct decompression_data *data,
                                     uint16_t length, uint16_t distance)
{
    uint16_t copy_start_pos = (data->back_refs_pos - distance +
                               MAX_DISTANCE) % MAX_DISTANCE;
    if (!data->back_refs_filled &&
        copy_start_pos >= data->back_refs_pos) {
        fprintf(stderr, "Invalid back reference for copying bytes\n");
        return false;
    }
    uint16_t tmp = copy_start_pos;
    uint8_t bytes_to_copy[258]; // max length 258
    for (uint16_t i = 0; i < length; ++i) {
        bytes_to_copy[i] = data->back_refs[tmp];
        tmp = (tmp + 1) % MAX_DISTANCE;
        if (tmp == data->back_refs_pos) {
            tmp = copy_start_pos;
        }
    }

    // now copy them to back_refs and out_buf
    bool success = handle_literal_codes(data, bytes_to_copy, length);
    if (!success) {
        fprintf(stderr, "Failed to handle literal bytes\n");
        return false;
    }

    return true;
}

static bool decompress_block_type_01(struct decompression_data *data)
{
    uint8_t lengths[288];

    // fixed huffman code lengths for block type 01
    // ref: https://www.ietf.org/rfc/rfc1951.txt section 3.2.6
    for (uint8_t i = 0; i < 144; ++i)
        lengths[i] = 8;
    for (uint16_t i = 144; i < 256; ++i)
        lengths[i] = 9;
    for (uint16_t i = 256; i < 280; ++i)
        lengths[i] = 7;
    for (uint16_t i = 280; i <= 287; ++i)
        lengths[i] = 8;

    struct node *root = create_huffman_tree(lengths, 288, 15);
    if (root == NULL) {
        fprintf(stderr, "Failed to create huffman tree in block type 01\n");
        return false;
    }

    while (true) {
        struct node *edge_node = find_huffman_code(data, root);
        if (edge_node == NULL) {
            fprintf(stderr, "Could not find huffman code in block type 01\n");
            return false;
        }
        if (!is_literal_length_code(edge_node->code)) {
            fprintf(stderr, "Invalid literal length code in block type 01\n");
            return false;
        }
        uint16_t code = (uint16_t) edge_node->code;
        // block end marker
        if (code == 256)
            break;

        if (is_literal_code(code)) {
            uint8_t byte = (uint8_t) code;
            bool success = handle_literal_codes(data, &byte, 1);
            if (!success) {
                fprintf(stderr, "Failed to handle literal code in "
                        "block type 01\n");
                return false;
            }
        } else if (is_length_code(code)) {
            uint16_t length = 0;
            bool success = length_from_length_code(data, code, &length);
            if (!success) {
                fprintf(stderr, "Failed to get length from length code "
                        "in block type 01\n");
                return false;
            }

            // fixed 5 bits for distance codes
            uint16_t tmp = 0;
            success = read_bits(data, 5, &tmp);
            if (!success) {
                fprintf(stderr, "Failed to read distance code in "
                        "block type 01\n");
                return false;
            }
            uint8_t distance_code = (uint8_t) tmp;
            uint16_t distance = 0;
            success = distance_from_distance_code(data, distance_code,
                                                  &distance);
            if (!success) {
                fprintf(stderr, "Failed to get distance from distance code "
                        "in block type 01\n");
                return false;
            }
            success = copy_bytes_from_distance(data, length, distance);
            if (!success) {
                fprintf(stderr, "Failed to copy bytes from back reference "
                        "in block type 01\n");
                return false;
            }
        }
    }

    free_huffman_tree(root);
    return true;
}

static bool decompress_block_type_10(struct decompression_data *data)
{
    uint16_t tmp = 0;

    // HLIT 5 bits, HDIST 5 bits, HCLEN 4 bits
    // ref: https://www.ietf.org/rfc/rfc1951.txt section 3.2.7

    // HLIT
    bool success = read_bits(data, 5, &tmp);
    if (!success) {
        fprintf(stderr, "Failed to read HLIT in block type 10\n");
        return false;
    }
    uint8_t HLIT = (uint8_t) tmp;
    // number of literal length codes
    uint16_t ll_code_cnt = (uint16_t) HLIT + 257;
    if (ll_code_cnt < 257 || ll_code_cnt > 286) {
        fprintf(stderr, "Expecting ll code count to be between 257 to 285 "
                " in block type 10\n");
        return false;
    }

    // HDIST
    success = read_bits(data, 5, &tmp);
    if (!success) {
        fprintf(stderr, "Failed to read HDIST in block type 10\n");
        return false;
    }
    uint8_t HDIST = (uint8_t) tmp;
    // number of distance codes
    uint8_t d_code_cnt = HDIST + 1;
    if (d_code_cnt < 1 || d_code_cnt > 32) {
        fprintf(stderr, "Expecting distance code count to be between "
                "1 to 31 in block type 10\n");
        return false;
    }

    // HCLEN
    success = read_bits(data, 4, &tmp);
    if (!success) {
        fprintf(stderr, "Failed to read HCLEN in block type 10\n");
        return false;
    }
    uint8_t HCLEN = (uint8_t) tmp;
    // number of code length codes
    uint8_t cl_code_cnt = HCLEN + 4;
    if (cl_code_cnt < 4 || cl_code_cnt > 19) {
        fprintf(stderr, "Expecting cl code count to be between "
                "4 and 18 in block type 10\n");
        return false;
    }

    // ref: https://www.rfc-editor.org/rfc/rfc1951.txt section 3.2.7
    uint8_t cl_code_serial[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12,
                                  3, 13, 2, 14, 1, 15};
    uint8_t cl_code_lengths[19];
    for (uint8_t i = 0; i < 19; ++i)
        cl_code_lengths[i] = 0;

    for (uint8_t i = 0; i < cl_code_cnt; ++i) {
        // cl code lengths are 3 bits each
        success = read_bits(data, 3, &tmp);
        if (!success) {
            fprintf(stderr, "Failed to read code length code in "
                    "block type 10\n");
            return false;
        }
        cl_code_lengths[cl_code_serial[i]] = (uint8_t) tmp;
    }

    struct node *cl_root = create_huffman_tree(cl_code_lengths, 19, 7);
    if (cl_root == NULL) {
        fprintf(stderr, "Failed to generate binary tree for block type 10\n");
        return false;
    }

    uint8_t ll_code_lengths[286];
    for (uint16_t i = 0; i < 286; ++i)
        ll_code_lengths[i] = 0;

    uint8_t d_code_lengths[32];
    for (uint8_t i = 0; i < 32; ++i)
        d_code_lengths[i] = 0;

    // The code length repeat codes can cross from HLIT + 257 to the
    // HDIST + 1 code lengths.  In other words, all code lengths form
    // a single sequence of HLIT + HDIST + 258 values.
    // ref: https://www.rfc-editor.org/rfc/rfc1951.txt section 3.2.7

    uint8_t previous_code_length = 0;
    uint16_t total = ll_code_cnt + d_code_cnt;
    uint16_t cnt = 0;

    while (cnt < total) {
        struct node *edge_node = find_huffman_code(data, cl_root);
        if (edge_node == NULL) {
            fprintf(stderr, "Could not find huffman code in block type 10\n");
            return false;
        }
        if (!is_code_length_code(edge_node->code)) {
            fprintf(stderr, "Invalid code length code found in "
                    "block type 10\n");
            return false;
        }

        uint8_t code = (uint8_t) edge_node->code;
        if (code == 16 && cnt == 0) {
            fprintf(stderr, "Repeat code 16 without any previous "
                    "code length in block type 10\n");
            return false;
        }

        if (code >= 0 && code <= 15) {
            if (cnt < ll_code_cnt) {
                ll_code_lengths[cnt] = code;
            } else {
                d_code_lengths[cnt - ll_code_cnt] = code;
            }
            previous_code_length = code;
            cnt++;
        } else if (code == 16) {
            // extra 2 bits for repeat code 16
            // 0 = 3, ... , 3 = 6
            success = read_bits(data, 2, &tmp);
            if (!success) {
                fprintf(stderr, "Failed to read extra 2 bits for "
                        "code length 16 in block type 10\n");
                return false;
            }
            tmp += 3;
            while (tmp--) {
                if (cnt >= total) {
                    fprintf(stderr, "Repeat code exceeds HLIT + HDIST + 258 "
                            "values in block type 10\n");
                    return false;
                }
                if (cnt < ll_code_cnt) {
                    ll_code_lengths[cnt] = previous_code_length;
                } else {
                    d_code_lengths[cnt - ll_code_cnt] = previous_code_length;
                }
                cnt++;
            }
        } else if (code == 17 || code == 18) {
            // copy zero 3 - 10 times (byte == 17)
            // copy zero 11 - 138 times (byte == 18)
            uint8_t extra_bits = code == 17 ? 3 : 7;
            uint8_t plus = code == 17 ? 3 : 11;
            success = read_bits(data, extra_bits, &tmp);
            if (!success) {
                fprintf(stderr, "Failed to read extra bits for repeat code %d "
                        "in block type 10\n", code);
                return false;
            }
            previous_code_length = 0;
            tmp += plus;
            while (tmp--) {
                if (cnt >= total) {
                    fprintf(stderr, "Repeat code exceeds HLIT + HDIST + 258 "
                            "values in block type 10\n");
                    return false;
                }
                if (cnt < ll_code_cnt) {
                    ll_code_lengths[cnt] = 0;
                } else {
                    d_code_lengths[cnt - ll_code_cnt] = 0;
                }
                cnt++;
            }
        }
    }

    free_huffman_tree(cl_root);

    struct node *ll_root = create_huffman_tree(ll_code_lengths, ll_code_cnt,
                                               15);
    if (ll_root == NULL) {
        fprintf(stderr, "Failed to create binary tree ll codes in "
                "block type 10\n");
        return false;
    }

    struct node *d_root = create_huffman_tree(d_code_lengths, d_code_cnt, 15);
    if (d_root == NULL) {
        fprintf(stderr, "Failed to generate binary tree distance codes "
                "in block type 10\n");
        return false;
    }

    while (true) {
        struct node *edge_node = find_huffman_code(data, ll_root);
        if (edge_node == NULL) {
            fprintf(stderr, "Failed to find huffman code for length "
                    "in block type 10\n");
            return false;
        }
        if (!is_literal_length_code(edge_node->code)) {
            fprintf(stderr, "Expecting valid literal length code "
                    "in block type 10\n");
            return false;
        }

        uint16_t code = (uint16_t) edge_node->code;
        // block end marker
        if (code == 256)
            break;

        if (is_literal_code(code)) {
            uint8_t byte = (uint8_t) code;
            success = handle_literal_codes(data, &byte, 1);
            if (!success) {
                fprintf(stderr, "Failed to handle literal code "
                        "in block type 10\n");
                return false;
            }
        } else if (is_length_code(code)) {
            uint16_t length = 0;
            success = length_from_length_code(data, code, &length);
            if (!success) {
                fprintf(stderr, "Failed to get length from length code "
                        "in block type 10\n");
                return false;
            }

            struct node *edge = find_huffman_code(data, d_root);
            if (edge == NULL) {
                fprintf(stderr, "Could not find huffman code for distance "
                        "in block type 10\n");
                return false;
            }

            uint8_t distance_code = (uint8_t) edge->code;
            uint16_t distance = 0;
            success = distance_from_distance_code(data, distance_code,
                                                  &distance);
            if (!success) {
                fprintf(stderr, "Failed to get distance from distance code "
                        "in block type 10\n");
                return false;
            }

            success = copy_bytes_from_distance(data, length, distance);
            if (!success) {
                fprintf(stderr, "Failed to copy from back references "
                        "in block type 10\n");
                return false;
            }
        }
    }

    free_huffman_tree(ll_root);
    free_huffman_tree(d_root);

    return true;
}

static bool decompress_blocks(uint8_t *buf, size_t buf_len, size_t *buf_pos,
                              FILE *f)
{
    uint8_t back_refs[MAX_DISTANCE];
    uint8_t out_buf[OUT_BUF_SIZE];

    struct decompression_data data;
    data.buf = buf;
    data.buf_len = buf_len;
    data.buf_pos = *buf_pos;
    data.byte_pos = 0;
    data.back_refs = back_refs;
    data.back_refs_pos = 0;
    data.back_refs_filled = false;
    data.out_buf = out_buf;
    data.out_pos = 0;
    data.f = f;

    while (true) {
        if (data.buf_pos >= data.buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }

        // 3 header bits
        // BFINAL (1 bit)
        bool BFINAL = (buf[data.buf_pos] >> data.byte_pos) & 1;
        increment_bit_position(&data);
        if (data.buf_pos >= data.buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }

        // BTYPE (2 bits)
        bool BTYPE_LSB = (buf[data.buf_pos] >> data.byte_pos) & 1;
        increment_bit_position(&data);
        if (data.buf_pos >= data.buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }
        bool BTYPE_MSB = (buf[data.buf_pos] >> data.byte_pos) & 1;
        increment_bit_position(&data);
        if (data.buf_pos >= data.buf_len) {
            fprintf(stderr, "Unexpected buffer length\n");
            return false;
        }

        if (BTYPE_MSB == 1 && BTYPE_LSB == 1) {
            fprintf(stderr, "Error BTYPE\n");
            return false;
        }

        if (BTYPE_MSB == 0 && BTYPE_LSB == 0) {
            bool success = decompress_block_type_00(&data);
            if (!success) {
                fprintf(stderr, "Failed to decompress block type 00\n");
                return false;
            }
        } else if (BTYPE_MSB == 0 && BTYPE_LSB == 1) {
            bool success = decompress_block_type_01(&data);
            if (!success) {
                fprintf(stderr, "Failed to decompress block type 01\n");
                return false;
            }
        } else if (BTYPE_MSB == 1 && BTYPE_LSB == 0) {
            bool success = decompress_block_type_10(&data);
            if (!success) {
                fprintf(stderr, "Failed to decompress block type 10\n");
                return false;
            }
        }

        if (BFINAL)
            break;
    }

    if (data.out_pos) {
        if (fwrite(data.out_buf, 1, data.out_pos, data.f) != data.out_pos) {
            fprintf(stderr, "Could not write full buffer\n");
            return false;
        }
        data.out_pos = 0;
    }

    // CRC32 starts at (next) byte boundary
    if (data.byte_pos)
        data.buf_pos++;

    *buf_pos = data.buf_pos;
    return true;
}

bool decompress_members(uint8_t *buf, size_t buf_len, FILE *f)
{
    size_t buf_pos = 0;

    while (true) {
        bool success = check_member_header(buf, buf_len, &buf_pos);
        if (!success) {
            fprintf(stderr, "Invalid member header\n");
            return false;
        }

        success = decompress_blocks(buf, buf_len, &buf_pos, f);
        if (!success) {
            fprintf(stderr, "Failed to decompress blocks\n");
            return false;
        }

        success = check_member_trailer(buf, buf_len, &buf_pos);
        if (!success) {
            fprintf(stderr, "Invalid member trailer\n");
            return false;
        }

        if (buf_pos == buf_len)
            break;
    }

    return true;
}
