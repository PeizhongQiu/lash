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
    Dir *init_dir = malloc(sizeof(Dir));
    init_dir->depth = depth;
    init_dir->mseg = malloc(sizeof(MulSegment *) * (1 << depth));

    int i;
    for (i = 0; i < (1 << depth); ++i)
    {
        MulSegment *newMseg = malloc(sizeof(MulSegment));
        newMseg->metadata[0] = 0;
        newMseg->metadata[1] = 0;
        newMseg->metadata[2] = 0;
        newMseg->metadata[3] = depth;

        Segment *newSeg0 = getNvmBlock(0);
        Segment *newSeg1 = getNvmBlock(0);
        //Segment *newSeg2 = getNvmBlock(0);
        newMseg->seg[0] = newSeg0;
        newMseg->seg[1] = newSeg1;
        newMseg->seg[2] = NULL;
        init_dir->mseg[i] = newMseg;
    }
    hash->dir = init_dir;
}

int hashInsert(Hash *hash, uint64_t new_key, uint64_t new_value)
{
    uint64_t hash_key = hash_64(new_key);
    Dir *dir = hash->dir;
    uint64_t index = hash_key >> (KEY_BIT - dir->depth - 1);
    MulSegment *mseg = dir->mseg[index >> 1];
    Segment *seg = mseg->seg[index & 1];
    uint64_t seg_key_num = mseg->metadata[index & 1];
    if (seg_key_num < SEGMENT_SIZE)
    {
        seg->_[seg_key_num].key = new_key;
        seg->_[seg_key_num].value = new_value;
        pmem_persist(&seg->_[seg_key_num], sizeof(Pair));
        ++mseg->metadata[index & 1];
        return 0;
    }
    else
    {
        MulSegment *newMseg = malloc(sizeof(MulSegment));
        newMseg->seg[0] = getNvmBlock(0);
        newMseg->seg[1] = getNvmBlock(0);
        newMseg->seg[2] = mseg->seg[1];
        newMseg->metadata[0] = 0;
        newMseg->metadata[1] = 0;
        newMseg->metadata[2] = SEGMENT_SIZE;

        mseg->seg[2] = mseg->seg[0];

        //update dir
        uint64_t i;
        if (mseg->metadata[3] < dir->depth)
        {
            uint64_t stride = 1 << (dir->depth - mseg->metadata[3]);
            uint64_t loc = (index >> 1) - ((index >> 1) & (stride - 1));
            for (i = 0; i < stride / 2; ++i)
            {
                dir->mseg[loc + stride / 2 + i] = newMseg;
            }
        }
        else
        {
            Dir *new_dir = malloc(sizeof(Dir));
            new_dir->depth = dir->depth + 1;
            new_dir->mseg = malloc(sizeof(MulSegment *) * (1 << new_dir->depth));
            for (i = 0; i < (1 << (new_dir->depth)); ++i)
            {
                if (i == (index >> 1))
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
        for (i = 0; i < SEGMENT_SIZE; ++i)
        {
            uint64_t cur_hash_key = hash_64(newMseg->seg[2]->_[i].key);
            uint64_t index = (cur_hash_key >> (KEY_BIT - mseg->metadata[3] - 2)) & 1;
            Segment *new_seg = newMseg->seg[index];
            new_seg->_[newMseg->metadata[index]].key = newMseg->seg[2]->_[i].key;
            new_seg->_[newMseg->metadata[index]].value = newMseg->seg[2]->_[i].value;
            pmem_persist(&new_seg->_[newMseg->metadata[index]], sizeof(Pair));
            ++newMseg->metadata[index];
            --newMseg->metadata[2];
        }
        newMseg->metadata[3] = mseg->metadata[3] + 1;
        //free newMseg->seg[2]
        newMseg->seg[2] = NULL;

        mseg->seg[0] = getNvmBlock(0);
        mseg->seg[1] = getNvmBlock(0);
        //将newMseg->seg[2]分裂到newMseg->seg[0]和newMseg->seg[1]
        mseg->metadata[0] = 0;
        mseg->metadata[1] = 0;
        mseg->metadata[2] = SEGMENT_SIZE;

        for (i = 0; i < SEGMENT_SIZE; ++i)
        {
            uint64_t cur_hash_key = hash_64(mseg->seg[2]->_[i].key);
            uint64_t index = (cur_hash_key >> (KEY_BIT - mseg->metadata[3] - 2)) & 1;
            Segment *new_seg = mseg->seg[index];
            new_seg->_[mseg->metadata[index]].key = mseg->seg[2]->_->key;
            new_seg->_[mseg->metadata[index]].value = mseg->seg[2]->_->value;
            pmem_persist(&new_seg->_[newMseg->metadata[index]], sizeof(Pair));
            ++mseg->metadata[index];
            --newMseg->metadata[2];
        }
        ++mseg->metadata[3];
        mseg->seg[2] = NULL;
        hashInsert(hash,new_key,new_value);
    }
    return 0;
}

uint64_t hashSearch(Hash *hash, uint64_t key)
{
    uint64_t hash_key = hash_64(key);
    Dir *dir = hash->dir;
    uint64_t index = hash_key >> (KEY_BIT - dir->depth - 1);
    MulSegment *mseg = dir->mseg[index >> 1];
    Segment *seg = mseg->seg[index & 1];
    int i;
    for (i = 0; i < SEGMENT_SIZE; ++i)
    {
        if(seg->_[i].key == key)
            return seg->_[i].value;
    }
    return 0;
}