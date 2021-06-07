#ifndef LASH_H
#define LASH_H

#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define KEY_BIT 64
#define SEGMENT_INDEX_BIT 8
#define BUCKET_SIZE 16
#define SEGMENT_SIZE ((1 << SEGMENT_INDEX_BIT) * BUCKET_SIZE)

typedef struct Pair{
    uint64_t key;
    uint64_t value;
}Pair;

typedef struct Segment{
    Pair _[SEGMENT_SIZE];
}Segment;

typedef struct MulSegment{
    Segment *seg[3];
    uint64_t metadata[4];   //0-2 代表每个seg数量，3代表depth
}MulSegment;

typedef struct Dir{
    MulSegment **mseg;
    int depth;
}Dir;

typedef struct Hash{
    Dir *dir;
}Hash;

void hashInit(Hash *hash, uint64_t depth);
int hashInsert(Hash *hash, uint64_t new_key, uint64_t new_value);
uint64_t hashSearch(Hash *hash, uint64_t key);

#endif