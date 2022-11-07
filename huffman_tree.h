#ifndef HUFFMAN_TREE
#define HUFFMAN_TREE

#include <inttypes.h>

struct node {
    struct node *left;
    struct node *right;
    int16_t code;
};

struct node *create_huffman_tree(uint8_t *code_lengths, uint16_t length,
                                 uint8_t max_huffman_code_length);
void free_huffman_tree(struct node *root);

#endif
