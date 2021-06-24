#ifndef LASH_H
#define LASH_H

#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define KEY_BIT 64
#define BUCKET_INDEX_BIT 8
#define FP_BIT 8
#define BUCKET_INDEX_MASK 0xff
#define SEGMENT_SIZE (1 << BUCKET_INDEX_BIT)
#define STASH_SIZE 16
#define BUCKET_SIZE 7

typedef struct Pair
{
    uint64_t key;
    uint64_t value;
} Pair;

typedef struct BucketMetadata
{
    uint16_t overflowIndex; //4 bits for the index of each overflow KV
    uint8_t bitmap;
    uint8_t membership;
    uint8_t over_bitmap_membership;
    uint8_t fp[11]; //4 for stash
}BucketMetadata;

#ifdef CHEN_VERSION
typedef struct Line0
{
    BucketMetadata metadata;
    Pair data[3];
}Line0;
typedef struct Line1
{
    Pair data[4];
}Line1;
typedef struct Bucket
{
    Line0 line0;
    Line1 line1;
} Bucket;
#else
typedef struct Bucket
{
    BucketMetadata metadata;
    Pair data[BUCKET_SIZE];
} Bucket;
#endif

typedef struct Stash
{
    Pair data[16];
    uint16_t bitmap; 
}Stash;

typedef struct Segment
{
    Bucket _[SEGMENT_SIZE];
    Stash stash;
    uint64_t metadata; //后8位做depth
} Segment;

typedef struct MulSegment
{
    Segment *seg[3];
    uint64_t metadata; //后8位做depth
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