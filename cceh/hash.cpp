#include "hash.h"
#include "memory_management.h"

//hash function
size_t unaligned_load(const char *p)
{
    size_t result;
    __builtin_memcpy(&result, p, sizeof(result));
    return result;
}
size_t load_bytes(const char *p, int n)
{
    size_t result = 0;
    --n;
    do
        result = (result << 8) + (unsigned char)(p[n]);
    while (--n >= 0);
    return result;
}
size_t shift_mix(size_t v)
{
    return v ^ (v >> 47);
}
size_t _Hash_bytes(const void *ptr, size_t len, size_t seed)
{
    static const size_t mul = (((size_t)0xc6a4a793UL) << 32UL) + (size_t)0x5bd1e995UL;
    const char *const buf = (const char *)(ptr);

    // Remove the bytes not divisible by the sizeof(size_t).  This
    // allows the main loop to process the data as 64-bit integers.
    const size_t len_aligned = len & ~(size_t)0x7;
    const char *const end = buf + len_aligned;
    size_t hash = seed ^ (len * mul);
    const char *p = buf;
    for (; p != end; p += 8)
    {
        const size_t data = shift_mix(unaligned_load(p) * mul) * mul;
        hash ^= data;
        hash *= mul;
    }
    if ((len & 0x7) != 0)
    {
        const size_t data = load_bytes(end, len & 0x7);
        hash ^= data;
        hash *= mul;
    }
    hash = shift_mix(hash) * mul;
    hash = shift_mix(hash);
    return hash;
}
size_t hash_64(size_t val)
{
    return _Hash_bytes(&val, sizeof(size_t), 0xc70697UL);
    //0xc70f6907UL
}

void hashInit(Hash *hash, uint64_t depth)
{
    Dir *init_dir = (Dir *)malloc(sizeof(Dir));
    init_dir->depth = depth;
    init_dir->mseg = (MulSegment **)malloc(sizeof(MulSegment *) * (1 << depth));

    int i;
    for (i = 0; i < (1 << depth); ++i)
    {
        MulSegment *newMseg = (MulSegment *)malloc(sizeof(MulSegment));
        newMseg->local_depth = depth;

        Segment *newSeg0 = getNvmBlock(0);
        memset(newSeg0, 0, sizeof(Segment));
        newSeg0->pattern = i << 1;
        Segment *newSeg1 = getNvmBlock(0);
        memset(newSeg1, 0, sizeof(Segment));
        newSeg1->pattern = (i << 1) + 1; 

        pmem_persist(newSeg0, sizeof(Segment));
        pmem_persist(newSeg1, sizeof(Segment));
        //Segment *newSeg2 = getNvmBlock(0);
        newMseg->seg[0] = newSeg0;
        newMseg->seg[1] = newSeg1;
        newMseg->seg[2] = NULL;
        init_dir->mseg[i] = newMseg;
    }
    hash->dir = init_dir;
}

uint32_t segmentInsert(Segment *seg, uint64_t new_key, uint64_t new_value, 
                        uint64_t hash_key, int ispmem, uint64_t local_depth)
{
    uint64_t bucket_index = (hash_key & kMask) * kNumPairPerCacheLine;
    uint64_t pattern = seg->pattern;
    uint64_t i;
    for (i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i)
    {
        uint64_t slot = (bucket_index + i) % kNumSlot;

        if (seg->_[slot].key == INVALID ||
            (hash_64(seg->_[slot].key) >> (KEY_BIT - local_depth - 1)) != pattern)
        {
            seg->_[slot].value = new_value;
            seg->_[slot].key = new_key;
            pmem_persist(&seg->_[slot],sizeof(Pair));
            return 0;
        }
    }
    return 1;
}

void splitSeg(MulSegment *newMseg, uint64_t depth)
{   
    uint64_t pattern = newMseg->seg[2]->pattern;
    uint64_t i;
    for (i = 0; i < kNumSlot; ++i)
    {
        uint64_t key = newMseg->seg[2]->_[i].key;
        uint64_t hash_key = hash_64(key);
        if (key != INVALID && (hash_key >> (KEY_BIT - newMseg->local_depth - 1)) == pattern)
        {
            uint64_t index_seg = (hash_key >> (KEY_BIT - newMseg->local_depth - 2)) & 1;
            Segment *seg = newMseg->seg[index_seg];
            segmentInsert(seg, key, newMseg->seg[2]->_[i].value, hash_key, 0, depth + 1);
        }
    }
    
    pmem_persist(newMseg->seg[0], sizeof(Segment));
    pmem_persist(newMseg->seg[1], sizeof(Segment));
    newMseg->local_depth = depth + 1;
    newMseg->seg[2] = NULL;
}

uint32_t hashInsert(Hash *hash, uint64_t new_key, uint64_t new_value)
{
    uint64_t hash_key = hash_64(new_key);
    Dir *dir = hash->dir;
    uint64_t index_dir = hash_key >> (KEY_BIT - dir->depth);
    MulSegment *mseg = dir->mseg[index_dir];
    uint64_t index_seg = (hash_key >> (KEY_BIT - mseg->local_depth - 1)) & 1;
    Segment *seg = mseg->seg[index_seg];

    uint32_t segState = segmentInsert(seg, new_key, new_value, hash_key, 1, mseg->local_depth);

    if (segState)
    {
        MulSegment *newMseg = (MulSegment *)malloc(sizeof(MulSegment));
        newMseg->seg[2] = mseg->seg[1];
        mseg->seg[2] = mseg->seg[0];

        newMseg->seg[0] = getNvmBlock(0);
        newMseg->seg[0]->pattern = newMseg->seg[2]->pattern << 1;
        newMseg->seg[1] = getNvmBlock(0);
        newMseg->seg[1]->pattern = (newMseg->seg[2]->pattern << 1) + 1;
        
        mseg->seg[0] = getNvmBlock(0);
        mseg->seg[0]->pattern = mseg->seg[2]->pattern << 1;
        mseg->seg[1] = getNvmBlock(0);
        mseg->seg[1]->pattern = (mseg->seg[2]->pattern << 1) + 1;
        //update dir
        uint64_t i;
        if (mseg->local_depth < dir->depth)
        {
            uint64_t stride = 1 << (dir->depth - mseg->local_depth);
            uint64_t loc = index_dir - (index_dir & (stride - 1));
            for (i = 0; i < stride / 2; ++i)
            {
                dir->mseg[loc + stride / 2 + i] = newMseg;
            }
        }
        else
        {
            Dir *new_dir = (Dir *)malloc(sizeof(Dir));
            new_dir->depth = dir->depth + 1;
            new_dir->mseg = (MulSegment **)malloc(sizeof(MulSegment *) * (1 << new_dir->depth));
            for (i = 0; i < (1 << (dir->depth)); ++i)
            {
                if (i == index_dir)
                {
                    new_dir->mseg[i * 2] = dir->mseg[i];
                    new_dir->mseg[i * 2 + 1] = newMseg;
                }
                else
                {
                    new_dir->mseg[i * 2] = dir->mseg[i];
                    new_dir->mseg[i * 2 + 1] = dir->mseg[i];
                }
            }
            free(hash->dir);
            hash->dir = new_dir;
        }

        //将newMseg->seg[2]分裂到newMseg->seg[0]和newMseg->seg[1]
        splitSeg(newMseg, mseg->local_depth);

        //将mseg->seg[2]分裂到mseg->seg[0]和mseg->seg[1]
        splitSeg(mseg, mseg->local_depth);

        return hashInsert(hash, new_key, new_value);
    }
    return 0;
}


//return 0 means can't search
uint64_t hashSearch(Hash *hash, uint64_t key)
{
    uint64_t hash_key = hash_64(key);
    // printf("search: %llx %llx\n",key,hash_key);
    Dir *dir = hash->dir;
    uint64_t index_dir = hash_key >> (KEY_BIT - dir->depth);
    MulSegment *mseg = dir->mseg[index_dir];
    uint64_t index_seg = (hash_key >> (KEY_BIT - mseg->local_depth - 1)) & 1;
    Segment *seg = mseg->seg[index_seg];
    uint64_t bucket_index = (hash_key & kMask) * kNumPairPerCacheLine;

    uint64_t i;
    for (i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i)
    {
        uint64_t slot = (bucket_index + i) % kNumSlot;
        if (seg->_[slot].key == key)
        {
            return seg->_[slot].value;
        }
    }
    return 0;
}