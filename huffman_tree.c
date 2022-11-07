#include "huffman_tree.h"
#include "huffman_code.h"

#include <inttypes.h>
#include <stdbool.h>
#include <malloc.h>

struct node *create_huffman_tree(uint8_t *code_lengths, uint16_t length,
                                 uint8_t max_huffman_code_length)
{
    if (length > 288) {
        fprintf(stderr, "Expecting number of code lengths to be less than 288\n");
        return NULL;
    }

    struct huffman codes[288];

    bool success = generate_huffman_codes(code_lengths, codes, length,
                                          max_huffman_code_length);
    if (!success) {
        fprintf(stderr, "Failed to generate huffman codes\n");
        return NULL;
    }

    size_t node_size = sizeof(struct node);
    struct node *root = malloc(node_size);

    if (root == NULL)
        return NULL;

    root->left = root->right = NULL;
    root->code = -1;

    for (uint16_t i = 0; i < length; ++i) {
        struct node *cur = root;
        uint8_t len = codes[i].len;

        for (uint8_t j = 0; j < len; ++j) {
            uint8_t bit = codes[i].huffman_code[j];
            if (bit != '1' && bit != '0') {
                free_huffman_tree(root);
                return NULL;
            }

            struct node *tmp = bit == '1' ? cur->right : cur->left;

            if ((j == len - 1 && tmp != NULL) ||
                (j != len - 1 && tmp != NULL && tmp->code != -1)) {
                free_huffman_tree(root);
                return NULL;
            }

            if (tmp == NULL) {
                tmp = malloc(node_size);
                if (tmp == NULL) {
                    free_huffman_tree(root);
                    return NULL;
                }
                tmp->left = tmp->right = NULL;
                tmp->code = -1;
            }

            if (j == len - 1)
                tmp->code = i;

            if (bit == '1')
                cur->right = tmp;
            else
                cur->left = tmp;

            cur = tmp;
        }
    }

    return root;
}

void free_huffman_tree(struct node *root)
{
    if (root == NULL)
        return;

    if (root->left != NULL)
        free_huffman_tree(root->left);
    
    if (root->right != NULL)
        free_huffman_tree(root->right);

    free(root);
    return;
}
