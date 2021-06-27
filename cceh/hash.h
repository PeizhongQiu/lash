#ifndef LASH_H
#define LASH_H

#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define KEY_BIT 64
#define BUCKET_INDEX_BIT 8
// #define kSegmentBits 8
#define kMask ((1 << BUCKET_INDEX_BIT) - 1)
// #define kShift kSegmentBits
#define kNumPairPerCacheLine 4
#define kNumCacheLine 4
// #define kSegmentSize ((1 << kSegmentBits) * sizeof(Pair) * kNumPairPerCacheLine)
#define kNumSlot ((1 << BUCKET_INDEX_BIT) * kNumPairPerCacheLine)
// #define key_size (8 * sizeof(size_t))
#define INVALID 0

typedef struct Pair
{
    uint64_t key;
    uint64_t value;
} Pair;

typedef struct Segment
{
    Pair _[kNumSlot];
    uint64_t pattern;
} Segment;

typedef struct MulSegment
{
    Segment *seg[3];
    uint64_t local_depth; //后8位做depth
} MulSegment;

typedef struct Dir
{
    MulSegment **mseg;
    int depth;
} Dir;

typedef struct Hash
{
    Dir *dir;
} Hash;

void hashInit(Hash *hash, uint64_t depth);
uint32_t hashInsert(Hash *hash, uint64_t new_key, uint64_t new_value);
uint64_t hashSearch(Hash *hash, uint64_t key);

#endif