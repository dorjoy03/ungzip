#include "decompress.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <malloc.h>

uint8_t *read_gzipped_file(char *filename, size_t *buf_len)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL)
        return NULL;

    int seek = fseek(f, 0, SEEK_END);
    if (seek != 0)
        goto fail;

    long tmp = (size_t) ftell(f);
    if (tmp == -1)
        goto fail;

    size_t len = (size_t) tmp;

    seek = fseek(f, 0, SEEK_SET);
    if (seek != 0)
        goto fail;

    uint8_t *buf = (uint8_t *) malloc(len);
    if (buf == NULL)
        goto fail;

    if (fread(buf, 1, len, f) != len) {
        free(buf);
        goto fail;
    }

    fclose(f);
    *buf_len = len;

    return buf;

 fail:
    fclose(f);
    return NULL;
}

void usage()
{
    printf("Usage: ungzip filename.gz\n");
    printf("       ungzip -h\n");
    return;
}

char *gzip_filename(char *cmd_arg)
{
    int len = strlen(cmd_arg);

    if (len < 4 || cmd_arg[len - 3] != '.' || cmd_arg[len - 2] != 'g' ||
        cmd_arg[len - 1] != 'z') {
        fprintf(stderr, "Expecting filename with .gz extension\n");
        return NULL;
    }

    return cmd_arg;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        usage();
        return 1;
    }

    if (strlen(argv[1]) == 2 && argv[1][0] == '-' && argv[1][1] == 'h') {
        usage();
        return 0;
    }

    char *filename = gzip_filename(argv[1]);

    if (filename == NULL)
        return 1;

    size_t buf_len = 0;
    uint8_t *buf = read_gzipped_file(filename, &buf_len);
    if (buf == NULL) {
        fprintf(stderr, "Failed to read %s file into memory\n", filename);
        return 1;
    }

    int32_t len = strlen(filename);
    filename[len - 3] = '\0';
    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        free(buf);
        fprintf(stderr, "Failed to open %s to write to\n", filename);
        return 1;
    }

    bool success = decompress_members(buf, buf_len, f);
    if (!success) {
        free(buf);
        fclose(f);
        remove(filename);
        fprintf(stderr, "Failed to decompress file. exiting...\n");
        return 1;
    }

    free(buf);
    fclose(f);
    printf("Successfully decompressed into %s\n", filename);
    return 0;
}
