#ifndef LASH_H
#define LASH_H

#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <cstring>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <pthread.h>
#include <libpmemobj.h>

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

class Hash;
class Directory;
class Segment;
POBJ_LAYOUT_BEGIN(HashTable);
POBJ_LAYOUT_ROOT(HashTable, Hash);
POBJ_LAYOUT_TOID(HashTable, struct Directory);
POBJ_LAYOUT_ROOT(HashTable, struct Segment);
POBJ_LAYOUT_TOID(HashTable, TOID(struct Segment));
POBJ_LAYOUT_END(HashTable);

typedef struct Pair
{
    uint64_t key;
    uint64_t value;
}Pair;

class Segment
{
    Pair _[kNumSlot];
    uint64_t pattern;
};

class MulSegment
{
    Segment *seg[3];
    uint64_t local_depth; //后8位做depth
};

class Dir
{
    MulSegment **mseg;
    int depth;
};

class Hash
{
public:
    void hashInit(uint64_t depth);
private:
    Dir *dir;
};


uint32_t hashInsert(Hash *hash, uint64_t new_key, uint64_t new_value);
uint64_t hashSearch(Hash *hash, uint64_t key);

#endif