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
#define BUCKET_SIZE 4
#define BUCKET_BITMAP_MASK ((1 << BUCKET_SIZE) - 1)
#define FULL_BUCKET_BITMAP ((1 << BUCKET_SIZE) - 1)

typedef struct Pair
{
    uint64_t key;
    uint64_t value;
} Pair;


typedef struct Bucket
{
    Pair data[BUCKET_SIZE];
} Bucket;


typedef struct Stash
{
    Pair data[STASH_SIZE];
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