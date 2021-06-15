#include "hash.h"
#include "memory_management.h"
#include <immintrin.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#include <mmintrin.h>

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
        newMseg->metadata = depth;

        Segment *newSeg0 = getNvmBlock(0);
        memset(newSeg0, 0, sizeof(Segment));
        Segment *newSeg1 = getNvmBlock(0);
        memset(newSeg1, 0, sizeof(Segment));

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

#ifdef CHEN_VERSION
int bucketInsert(Bucket *bck, uint64_t new_key, uint64_t new_value, 
                    uint64_t hash_key, uint16_t membership, int ispmem)
{
    uint16_t bitmap = bck->line0.metadata.bitmap_membership >> 8;
    int index = __builtin_ctz(~bitmap);
    uint16_t new_membership = bck->line0.metadata.bitmap_membership & 0xff;
    
    if(index < 3)
    {
        bck->line0.data[index].key = new_key;
        bck->line0.data[index].value = new_value;
        //update metadata
        bck->line0.metadata.bitmap_membership = ((bitmap | (1 << index)) << 8) + (new_membership | (membership << index));
        bck->line0.metadata.fp[index] = hash_key & 0xff;
        if(ispmem)
        {
            pmem_persist(&bck->line0, sizeof(Line0));
        }
        return ;
    }
    else
    {
        bck->line1.data[index - 3].key = new_key;
        bck->line1.data[index - 3].value = new_value;
        bck->line0.metadata.fp[index] = hash_key & 0xff;
        bitmap = bitmap | (1 << index);
        new_membership = new_membership | (membership << index);
        //data moving
        int i = 0;
        
        while((bitmap & 0x78) != 0x78 && i < 3)
        {
            index = __builtin_ctz(~(bitmap | 0x7));
            bck->line1.data[index - 3].key = bck->line0.data[i].key;
            bck->line1.data[index - 3].value = bck->line0.data[i].value;
            bitmap = (bitmap | (1 << index)) & ~(1 << i);
            bck->line0.metadata.fp[index] = bck->line0.metadata.fp[i];
            new_membership = (new_membership | (((new_membership >> i) & 1) << index)) & ~(1 << i);
            i++;
        }
        //update metadata
        bck->line0.metadata.bitmap_membership = (bitmap << 8) + new_membership;
        if(ispmem)
        {
            pmem_persist(&bck->line1, sizeof(Line1));
            pmem_persist(&bck->line0, sizeof(Line0));
        }
    }
}
#else
int bucketInsert(Bucket *bck, uint64_t new_key, uint64_t new_value, 
                    uint64_t hash_key, uint16_t membership, int ispmem)
{
    uint16_t bitmap = bck->metadata.bitmap_membership >> 8;
    int index = __builtin_ctz(~bitmap);
    bck->data[index].key = new_key;
    bck->data[index].value = new_value;
    if(ispmem)
    {
        pmem_persist(&bck->data[index], sizeof(Pair));
    }
    
    //update metadata
    bck->metadata.bitmap_membership = ((bitmap | (1 << index)) << 8) + 
                                    ((bck->metadata.bitmap_membership & 0xff) | (membership << index));
    bck->metadata.fp[index] = hash_key & 0xff;
    if(ispmem)
    {
        pmem_persist(&bck->metadata, sizeof(BucketMetadata));
    }
    return 0;
}
#endif

int stashInsert(Stash *stash, Bucket *bck, uint64_t new_key, uint64_t new_value, 
                    uint64_t hash_key, uint16_t membership, int ispmem)
{
    uint16_t bitmap = stash->bitmap;
    int index = __builtin_ctz(~bitmap);
    stash->data[index].key = new_key;
    stash->data[index].value = new_value;
    if(ispmem)
    {
        pmem_persist(&stash->data[index], sizeof(Pair));
    }
    //update metadata
    stash->bitmap = bitmap | (1 << index);
    if(ispmem)
    {
        pmem_persist(&stash->bitmap, sizeof(uint16_t));
    }

    #ifdef CHEN_VERSION
    uint8_t bucket_bitmap = bck->line0.metadata.over_bitmap_membership >> 4;
    uint8_t bucket_index = __builtin_ctz(~bucket_bitmap);


    bck->line0.metadata.over_bitmap_membership = ((bucket_bitmap | (1 << bucket_index)) << 4) + 
                                    ((bck->line0.metadata.over_bitmap_membership & 0xf) | (membership << index));
    bck->line0.metadata.overflowIndex = (bck->line0.metadata.overflowIndex & ~(0xf << (bucket_index * 4))) | (index << (bucket_index * 4));
    bck->line0.metadata.fp[bucket_index + 7] = hash_key & 0xff;
    if(ispmem)
    {
        pmem_persist(&bck->line0.metadata, sizeof(BucketMetadata));
    }
    #else
    uint8_t bucket_bitmap = bck->metadata.over_bitmap_membership >> 4;
    uint8_t bucket_index = __builtin_ctz(~bucket_bitmap);


    bck->metadata.over_bitmap_membership = ((bucket_bitmap | (1 << bucket_index)) << 4) + 
                                    ((bck->metadata.over_bitmap_membership & 0xf) | (membership << index));
    bck->metadata.overflowIndex = (bck->metadata.overflowIndex & ~(0xf << (bucket_index * 4))) | (index << (bucket_index * 4));
    bck->metadata.fp[bucket_index + 7] = hash_key & 0xff;
    if(ispmem)
    {
        pmem_persist(&bck->metadata, sizeof(BucketMetadata));
    }
    #endif
    return 0;
}

int segmentInsert(Segment *seg, uint64_t new_key, uint64_t new_value, uint64_t hash_key, int ispmem)
{
    uint64_t bucket_index = (hash_key >> FP_BIT) & BUCKET_INDEX_MASK;
    Bucket first_bucket = seg->_[bucket_index];
    Bucket second_bucket = seg->_[(bucket_index + 1)%SEGMENT_SIZE];

    uint16_t first_count = __builtin_popcount(first_bucket.metadata.bitmap_membership >> 8);
    uint16_t second_count = __builtin_popcount(second_bucket.metadata.bitmap_membership >> 8);
    if(first_count == second_count && first_count == BUCKET_SIZE)
    {
        //displace
        Bucket prev_bucket = seg->_[(bucket_index + SEGMENT_SIZE - 1) % SEGMENT_SIZE];
        if((prev_bucket.metadata.bitmap_membership >> 8) != 0x7f && 
        (first_bucket.metadata.bitmap_membership & 0xff) != 0)
        {
            uint16_t bitmap = first_bucket.metadata.bitmap_membership >> 8;
            uint16_t membership = first_bucket.metadata.bitmap_membership & 0xff;
            int index = __builtin_ctz(membership);
            bucketInsert(&prev_bucket,first_bucket.data[index].key,
                        first_bucket.data[index].value,hash_64(first_bucket.data[index].key), 0, ispmem);
            bitmap = bitmap & ~(1 << index);
            membership = membership & ~(1 << index);
            first_bucket.metadata.bitmap_membership = (bitmap << 8) + membership;
            if(ispmem)
            {
                pmem_persist(&first_bucket.metadata,sizeof(BucketMetadata));
            }
            first_bucket.data[index].key = new_key;
            first_bucket.data[index].value = new_value;
            if(ispmem)
            {
                pmem_persist(&first_bucket.data[index],sizeof(Pair));
            }
            bitmap = bitmap | (1 << index);
            first_bucket.metadata.bitmap_membership = (bitmap << 8) + membership;
            first_bucket.metadata.fp[index] = hash_key & 0xff;
            if(ispmem)
            {
                pmem_persist(&first_bucket.metadata,sizeof(BucketMetadata));
            }
            return 0;
        }
        Bucket next_bucket = seg->_[(bucket_index + 2) % SEGMENT_SIZE];
        if((next_bucket.metadata.bitmap_membership >> 8) != 0x7f && 
        (second_bucket.metadata.bitmap_membership & 0xff) != 0x7f)
        {
            uint16_t bitmap = second_bucket.metadata.bitmap_membership >> 8;
            uint16_t membership = second_bucket.metadata.bitmap_membership & 0xff;
            int index = __builtin_ctz(~membership);
            bucketInsert(&next_bucket,second_bucket.data[index].key,
                        second_bucket.data[index].value,hash_64(second_bucket.data[index].key), 1, ispmem);
            bitmap = bitmap & ~(1 << index);
            second_bucket.metadata.bitmap_membership = (bitmap << 8) + membership;
            if(ispmem)
            {
                pmem_persist(&second_bucket.metadata,sizeof(BucketMetadata));
            }
            
            second_bucket.data[index].key = new_key;
            second_bucket.data[index].value = new_value;
            if(ispmem)
            {
                pmem_persist(&second_bucket.data[index],sizeof(Pair));
            }
            
            bitmap = bitmap | (1 << index);
            membership = membership | (1 << index);
            second_bucket.metadata.bitmap_membership = (bitmap << 8) + membership;
            second_bucket.metadata.fp[index] = hash_key & 0xff;
            if(ispmem)
            {
                pmem_persist(&second_bucket.metadata,sizeof(BucketMetadata));
            }
            
            return 0;
        }
        else
        {
            //stash
            if(seg->stash.bitmap != 0xffff && (first_bucket.metadata.over_bitmap_membership >> 4) != 0xf)
            {
                stashInsert(&seg->stash,&first_bucket,new_key,new_value,hash_key,0,ispmem);
                return 0;
            }
            else if(seg->stash.bitmap != 0xffff && (second_bucket.metadata.over_bitmap_membership >> 4) != 0xf)
            {
                stashInsert(&seg->stash,&second_bucket,new_key,new_value,hash_key,1,ispmem);
                return 0;
            }
            //split
            else return 1;
        }
    }
    else if(first_count <= second_count)
    {
        bucketInsert(&first_bucket,new_key,new_value,hash_key, 0,ispmem);
        return 0;
    }
    else
    {
        bucketInsert(&second_bucket,new_key,new_value,hash_key, 1,ispmem);
        return 0;
    }
    
}

int hashInsert(Hash *hash, uint64_t new_key, uint64_t new_value)
{
    uint64_t hash_key = hash_64(new_key);
    Dir *dir = hash->dir;
    uint64_t index_dir = hash_key >> (KEY_BIT - dir->depth);
    MulSegment *mseg = dir->mseg[index_dir];
    uint64_t index_seg = (hash_key >> (KEY_BIT - mseg->metadata - 1)) & 1;
    Segment *seg = mseg->seg[index_seg];
    
    int segState = segmentInsert(seg, new_key, new_value, hash_key, 1);

    if (segState)
    {
        MulSegment *newMseg = malloc(sizeof(MulSegment));
        newMseg->seg[0] = getNvmBlock(0);
        newMseg->seg[1] = getNvmBlock(0);
        newMseg->seg[2] = mseg->seg[1];

        mseg->seg[2] = mseg->seg[0];

        //update dir
        uint64_t i,j;
        if (mseg->metadata < dir->depth)
        {
            uint64_t stride = 1 << (dir->depth - mseg->metadata);
            uint64_t loc = index_dir - (index_dir & (stride - 1));
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
        for (i = 0; i < SEGMENT_SIZE; ++i)
        {
            for(j = 0; j < BUCKET_SIZE; ++j)
            {
                if(newMseg->seg[2]->_->metadata.bitmap_membership & (1 << (j+8)))
                {
                    uint64_t re_hash_key = hash_64(newMseg->seg[2]->_[i].data[j].key);
                    uint64_t index_seg = (re_hash_key >> (KEY_BIT - mseg->metadata - 2)) & 1;
                    Segment *new_seg = newMseg->seg[index_seg];
                    segmentInsert(new_seg, newMseg->seg[2]->_[i].data[j].key, newMseg->seg[2]->_[i].data[j].value, re_hash_key, 0);
                }
            }
        }
        for (i = 0; i < STASH_SIZE; ++i)
        {
            
            if(newMseg->seg[2]->stash.bitmap & (1 << i))
            {
                uint64_t re_hash_key = hash_64(newMseg->seg[2]->stash.data[i].key);
                uint64_t index_seg = (re_hash_key >> (KEY_BIT - mseg->metadata - 2)) & 1;
                Segment *new_seg = newMseg->seg[index_seg];
                segmentInsert(new_seg, newMseg->seg[2]->stash.data[j].key, newMseg->seg[2]->stash.data[j].value, re_hash_key, 0);
            }
        }
        pmem_persist(newMseg->seg[0],sizeof(Segment));
        pmem_persist(newMseg->seg[1],sizeof(Segment));
        newMseg->metadata = mseg->metadata + 1;
        //free newMseg->seg[2]
        newMseg->seg[2] = NULL;

        mseg->seg[0] = getNvmBlock(0);
        mseg->seg[1] = getNvmBlock(0);
        //将newMseg->seg[2]分裂到newMseg->seg[0]和newMseg->seg[1]
        
        for (i = 0; i < SEGMENT_SIZE; ++i)
        {
            for(j = 0; j < BUCKET_SIZE; ++j)
            {
                if(mseg->seg[2]->_->metadata.bitmap_membership & (1 << (j+8)))
                {
                    uint64_t re_hash_key = hash_64(mseg->seg[2]->_[i].data[j].key);
                    uint64_t index_seg = (re_hash_key >> (KEY_BIT - mseg->metadata - 2)) & 1;
                    Segment *new_seg = mseg->seg[index_seg];
                    segmentInsert(new_seg, mseg->seg[2]->_[i].data[j].key, mseg->seg[2]->_[i].data[j].value, re_hash_key, 0);
                }
            }
        }
        for (i = 0; i < STASH_SIZE; ++i)
        {
            
            if(mseg->seg[2]->stash.bitmap & (1 << i))
            {
                uint64_t re_hash_key = hash_64(mseg->seg[2]->stash.data[i].key);
                uint64_t index_seg = (re_hash_key >> (KEY_BIT - mseg->metadata - 2)) & 1;
                Segment *new_seg = mseg->seg[index_seg];
                segmentInsert(new_seg, mseg->seg[2]->stash.data[j].key, mseg->seg[2]->stash.data[j].value, re_hash_key, 0);
            }
        }
        pmem_persist(mseg->seg[0],sizeof(Segment));
        pmem_persist(mseg->seg[1],sizeof(Segment));
        ++mseg->metadata;
        mseg->seg[2] = NULL;
        hashInsert(hash,new_key,new_value);
    }
    return 0;
}

uint64_t hashSearch(Hash *hash, uint64_t key)
{
    uint64_t hash_key = hash_64(key);
    Dir *dir = hash->dir;
    uint64_t index_dir = hash_key >> (KEY_BIT - dir->depth);
    MulSegment *mseg = dir->mseg[index_dir];
    uint64_t index_seg = (hash_key >> (KEY_BIT - mseg->metadata - 1)) & 1;
    Segment *seg = mseg->seg[index_seg];
    uint64_t bucket_index = (hash_key >> FP_BIT) & BUCKET_INDEX_MASK;
    Bucket first_bucket = seg->_[bucket_index];
    __m128i fp = _mm_set_epi8(first_bucket.metadata.fp[0],first_bucket.metadata.fp[1],first_bucket.metadata.fp[2],
                first_bucket.metadata.fp[3],first_bucket.metadata.fp[4],first_bucket.metadata.fp[5],
                first_bucket.metadata.fp[6],first_bucket.metadata.fp[7],first_bucket.metadata.fp[8],
                first_bucket.metadata.fp[9],first_bucket.metadata.fp[10], 0, 0, 0, 0, 0);
    uint8_t search_fp = hash_key & 0xff;
    __m128i key_data = _mm_set1_epi8(search_fp);
    __m128i rv_mask = _mm_cmpeq_epi8(fp, key_data);        \
    uint32_t mask = _mm_movemask_epi8(rv_mask);
    
    Bucket second_bucket = seg->_[(bucket_index + 1)%SEGMENT_SIZE];

}