#include "hash.h"
#include "memory_management.h"
#include <immintrin.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#include <mmintrin.h>

#ifdef CHEN_VERSION
#define getMetadata(bck) ((bck).line0.metadata)
#define pmem_persist_BucketMetadata(bck) {pmem_persist(&(bck).line0, sizeof(Line0));}
#else
#define getMetadata(bck) ((bck).metadata)
#define pmem_persist_BucketMetadata(bck) {pmem_persist(&(bck).metadata, sizeof(BucketMetadata));}
#endif

//get the bitmap of bucket
#define getBitmap(bck) (getMetadata(bck).bitmap)
//get the membership of bucket
#define getMembership(bck) (getMetadata(bck).membership)
//get the total number of pairs in bucket
#define getCount(bck) (__builtin_popcount(getMetadata(bck).bitmap))
//get the overflow bitmap
#define getOverflowBitmap(bck) (getMetadata(bck).over_bitmap_membership >> 4)
//get the overflow membership
#define getOverflowMembership(bck) (getMetadata(bck).over_bitmap_membership & 0xf)
//get the overflow index
#define getOverflowIndex(bck) (getMetadata(bck).overflowIndex)

#define setFp(bck, index, hash_key) {getMetadata(bck).fp[index] = (hash_key) & 0xff;}
#define setBitmap(bck, new_bitmap) {getMetadata(bck).bitmap = new_bitmap;}
#define setMembership(bck, new_membership) {getMetadata(bck).membership = new_membership;}
#define setOverflowBitmapMembership(bck, new_bitmap, new_membership) {getMetadata(bck).over_bitmap_membership = ((new_bitmap) << 4) + (new_membership);}
#define setOverflowIndex(bck, bucket_index, overflow_index)  \
    {getMetadata(bck).overflowIndex = (getMetadata(bck).overflowIndex & ~(0xf << (bucket_index << 2))) | (overflow_index << (bucket_index << 2));} 

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
uint32_t bucketInsert(Bucket *bck, uint64_t new_key, uint64_t new_value, 
                    uint64_t hash_key, uint16_t membership, int ispmem)
{
    uint8_t bitmap = getBitmap(*bck);
    int index = __builtin_ctz(~bitmap);
    uint8_t new_membership = getMembership(*bck);
    
    if(index < 3)
    {
        //metadata and data can be flushed in one flush
        bck->line0.data[index].key = new_key;
        bck->line0.data[index].value = new_value;
        //update metadata
        setBitmap(*bck, bitmap | (1 << index));
        setMembership(*bck, new_membership | (membership << index));
        setFp(*bck, index, hash_key);
        if(ispmem)
        {
            pmem_persist(&bck->line0, sizeof(Line0));
        }
        return 0;
    }
    else
    {
        bck->line1.data[index - 3].key = new_key;
        bck->line1.data[index - 3].value = new_value;
        setFp(*bck, index, hash_key);
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
            setFp(*bck, index, bck->line0.metadata.fp[i]);
            new_membership = (new_membership | (((new_membership >> i) & 1) << index)) & ~(1 << i);
            i++;
        }
        //update metadata
        setBitmap(*bck, bitmap);
        setMembership(*bck, new_membership);
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
    uint8_t bitmap = getBitmap(*bck);
    int index = __builtin_ctz(~bitmap);
    bck->data[index].key = new_key;
    bck->data[index].value = new_value;
    if(ispmem)
    {
        pmem_persist(&bck->data[index], sizeof(Pair));
    }
    
    //update metadata
    setBitmap(*bck, bitmap | (1 << index));
    setMembership(*bck, (*bck).metadata.membership | (membership << index));
    setFp(*bck, index, hash_key);
    printf("fp change: %x %p", bck->metadata.fp[index],bck);
    if(ispmem)
    {
        pmem_persist_BucketMetadata(*bck);
    }
    return 0;
}
#endif

#ifdef CHEN_VERSION
uint32_t bucketInsertDisplace(Bucket *bck, Bucket *displace_bck, uint8_t membership, int ispmem, 
                            uint64_t new_key, uint64_t new_value, uint64_t hash_key)
{
    uint8_t displace_bitmap = getBitmap(*displace_bck);
    uint8_t bck_membership = getMembership(*bck);
    if(displace_bitmap != 0x7f && bck_membership != membership)
    {
        uint8_t bck_bitmap = getBitmap(*bck);
        int bck_index = 0;
        if(membership & 1)
        {
            //membership = 7f, second_bucket to next_bucket
            bck_index = __builtin_ctz(~bck_membership);
            if(bck_index < 3)
                bucketInsert(displace_bck,bck->line0.data[bck_index].key,bck->line0.data[bck_index].value,
                        hash_64(bck->line0.data[bck_index].key), 1, ispmem);
            else 
                bucketInsert(displace_bck,bck->line1.data[bck_index - 3].key,bck->line1.data[bck_index - 3].value,
                        hash_64(bck->line1.data[bck_index - 3].key), 1, ispmem);
        }
        else
        {
            //membership = 0, first_bucket to prev_bucket
            bck_index = __builtin_ctz(bck_membership);
            if(bck_index < 3)
                bucketInsert(displace_bck,bck->line0.data[bck_index].key,bck->line0.data[bck_index].value,
                        hash_64(bck->line0.data[bck_index].key), 0, ispmem);
            else 
                bucketInsert(displace_bck,bck->line1.data[bck_index - 3].key,bck->line1.data[bck_index - 3].value,
                        hash_64(bck->line1.data[bck_index - 3].key), 0, ispmem);
        }
        
        bck_bitmap = bck_bitmap & ~(1 << bck_index);
        bck_membership = bck_membership & ~(1 << bck_index);
        setBitmap(*bck,bck_bitmap);
        setMembership(*bck,bck_membership);
        if(ispmem && bck_index > 3)
        {
            pmem_persist_BucketMetadata(*bck);
        }
        bck->data[bck_index].key = new_key;
        bck->data[bck_index].value = new_value;
        if(ispmem && bck_index > 3)
        {
            pmem_persist(&bck->line1.data[bck_index], sizeof(Pair));
        }
        bck_bitmap = bck_bitmap | (1 << bck_index);
        if(membership & 1)
        {
            bck_membership = bck_membership | (1 << bck_index);
        }
        setBitmap(*bck,bck_bitmap);
        setMembership(*bck,bck_membership);
        setFp(*bck,bck_index,hash_key);
        if(ispmem)
        {
            pmem_persist_BucketMetadata(*bck);
        }
        return 0;
    }
    return 1;
}
#else
int bucketInsertDisplace(Bucket *bck, Bucket *displace_bck, uint8_t membership, int ispmem, 
                            uint64_t new_key, uint64_t new_value, uint64_t hash_key)
{
    uint8_t displace_bitmap = getBitmap(*displace_bck);
    uint8_t bck_membership = getMembership(*bck);
    if(displace_bitmap != 0x7f && bck_membership != membership)
    {
        uint8_t bck_bitmap = getBitmap(*bck);
        int bck_index = 0;
        if(membership & 1)
        {
            bck_index = __builtin_ctz(~bck_membership);
            bucketInsert(displace_bck,bck->data[bck_index].key,bck->data[bck_index].value,
                        hash_64(bck->data[bck_index].key), 1, ispmem);
        }
        else
        {
            bck_index = __builtin_ctz(bck_membership);
            bucketInsert(displace_bck,bck->data[bck_index].key,bck->data[bck_index].value,
                        hash_64(bck->data[bck_index].key), 0, ispmem);
        }
        
        bck_bitmap = bck_bitmap & ~(1 << bck_index);
        bck_membership = bck_membership & ~(1 << bck_index);
        setBitmap(*bck,bck_bitmap);
        setMembership(*bck,bck_membership);
        if(ispmem)
        {
            pmem_persist_BucketMetadata(*bck);
        }
        bck->data[bck_index].key = new_key;
        bck->data[bck_index].value = new_value;
        if(ispmem)
        {
            pmem_persist(&bck->data[bck_index], sizeof(Pair));
        }
        bck_bitmap = bck_bitmap | (1 << bck_index);
        if(membership & 1)
        {
            bck_membership = bck_membership | (1 << bck_index);
        }
        setBitmap(*bck,bck_bitmap);
        setMembership(*bck,bck_membership);
        setFp(*bck,bck_index,hash_key);
        if(ispmem)
        {
            pmem_persist_BucketMetadata(*bck);
        }
        return 0;
    }
    return 1;
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

    uint8_t bucket_bitmap = getOverflowBitmap(*bck);
    uint8_t bucket_index = __builtin_ctz(~bucket_bitmap);
    setOverflowBitmapMembership(*bck, bucket_bitmap | (1 << bucket_index), getOverflowMembership(*bck) | (membership << index));
    setOverflowIndex(*bck,bucket_index,index);
    setFp(*bck,bucket_index + 7,hash_key);
    if(ispmem)
    {
        pmem_persist_BucketMetadata(*bck);
    }
    return 0;
}

int segmentInsert(Segment *seg, uint64_t new_key, uint64_t new_value, uint64_t hash_key, int ispmem)
{
    uint64_t bucket_index = (hash_key >> FP_BIT) & BUCKET_INDEX_MASK;
    Bucket &first_bucket = seg->_[bucket_index];
    Bucket &second_bucket = seg->_[(bucket_index + 1)%SEGMENT_SIZE];

    uint16_t first_count = getCount(first_bucket);
    uint16_t second_count = getCount(second_bucket);
    if(first_count == second_count && first_count == BUCKET_SIZE)
    {
        //displace
        Bucket &prev_bucket = seg->_[(bucket_index + SEGMENT_SIZE - 1) % SEGMENT_SIZE];
        int ok = bucketInsertDisplace(&first_bucket, &prev_bucket, 0, ispmem, new_key, new_value, hash_key);
        if(ok)
        {
            Bucket &next_bucket = seg->_[(bucket_index + 2) % SEGMENT_SIZE];
            ok = bucketInsertDisplace(&second_bucket, &next_bucket, 0x7f, ispmem, new_key, new_value, hash_key);
            if(ok)
            {
                //stash
                if(seg->stash.bitmap != 0xffff && getOverflowBitmap(first_bucket) != 0xf)
                {
                    stashInsert(&seg->stash,&first_bucket,new_key,new_value,hash_key,0,ispmem);
                    return 0;
                }
                if(seg->stash.bitmap != 0xffff && getOverflowBitmap(second_bucket) != 0xf)
                {
                    stashInsert(&seg->stash,&second_bucket,new_key,new_value,hash_key,1,ispmem);
                    return 0;
                }
                //split
                return 1;
            }
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

void splitSeg(MulSegment *newMseg, MulSegment *mseg)
{
    uint64_t i,j;
    for (i = 0; i < SEGMENT_SIZE; ++i)
        {
            for(j = 0; j < 3; ++j)
            {
                if(getBitmap(newMseg->seg[2]->_[i]) & (1 << j))
                {
                    #ifdef CHEN_VERSION
                    uint64_t cur_key = newMseg->seg[2]->_[i].line0.data[j].key;
                    uint64_t cur_value = newMseg->seg[2]->_[i].line0.data[j].value;
                    uint64_t re_hash_key = hash_64(cur_key);
                    #else
                    uint64_t cur_key = newMseg->seg[2]->_[i].data[j].key;
                    uint64_t cur_value = newMseg->seg[2]->_[i].data[j].value;
                    uint64_t re_hash_key = hash_64(cur_key);
                    #endif
                    
                    uint64_t index_seg = (re_hash_key >> (KEY_BIT - mseg->metadata - 2)) & 1;
                    Segment *new_seg = newMseg->seg[index_seg];
                    segmentInsert(new_seg, cur_key, cur_value, re_hash_key, 0);
                }
            }
            for(j = 3; j < BUCKET_SIZE; ++j)
            {
                if(getBitmap(newMseg->seg[2]->_[i]) & (1 << j))
                {
                    #ifdef CHEN_VERSION
                    uint64_t cur_key = newMseg->seg[2]->_[i].line1.data[j-3].key;
                    uint64_t cur_value = newMseg->seg[2]->_[i].line1.data[j-3].value;
                    uint64_t re_hash_key = hash_64(cur_key);
                    #else
                    uint64_t cur_key = newMseg->seg[2]->_[i].data[j].key;
                    uint64_t cur_value = newMseg->seg[2]->_[i].data[j].value;
                    uint64_t re_hash_key = hash_64(cur_key);
                    #endif

                    uint64_t index_seg = (re_hash_key >> (KEY_BIT - mseg->metadata - 2)) & 1;
                    Segment *new_seg = newMseg->seg[index_seg];
                    segmentInsert(new_seg, cur_key, cur_value, re_hash_key, 0);
                }
            }
        }
        for (i = 0; i < STASH_SIZE; ++i)
        {
            if(newMseg->seg[2]->stash.bitmap & (1 << i))
            {
                uint64_t cur_key = newMseg->seg[2]->stash.data[i].key;
                uint64_t cur_value = newMseg->seg[2]->stash.data[i].value;
                uint64_t re_hash_key = hash_64(cur_key);
                
                uint64_t index_seg = (re_hash_key >> (KEY_BIT - mseg->metadata - 2)) & 1;
                Segment *new_seg = newMseg->seg[index_seg];
                segmentInsert(new_seg, cur_key, cur_value, re_hash_key, 0);
            }
        }
        pmem_persist(newMseg->seg[0],sizeof(Segment));
        pmem_persist(newMseg->seg[1],sizeof(Segment));
        newMseg->metadata = mseg->metadata + 1;
        //free newMseg->seg[2]
        newMseg->seg[2] = NULL;
}

uint32_t hashInsert(Hash *hash, uint64_t new_key, uint64_t new_value)
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
        MulSegment *newMseg = (MulSegment *)malloc(sizeof(MulSegment));
        newMseg->seg[0] = getNvmBlock(0);
        newMseg->seg[1] = getNvmBlock(0);
        newMseg->seg[2] = mseg->seg[1];

        mseg->seg[2] = mseg->seg[0];
        mseg->seg[0] = getNvmBlock(0);
        mseg->seg[1] = getNvmBlock(0);
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
        splitSeg(newMseg,mseg);

        //将newMseg->seg[2]分裂到newMseg->seg[0]和newMseg->seg[1]
        splitSeg(mseg,mseg);

        return hashInsert(hash,new_key,new_value);
    }
    return 0;
}

uint64_t bucketSearch(uint16_t index, Bucket &bck, uint64_t key)
{
    #ifdef CHEN_VERSION
    if ((index & (1 << 0)) && bck.line0.data[0].key == key) 
    {
        return bck.line0.data[0].value;
    }
    if ((index & (1 << 1)) && bck.line0.data[1].key == key) 
    {
        return bck.line0.data[1].value;
    }
    if ((index & (1 << 2)) && bck.line0.data[2].key == key) 
    {
        return bck.line0.data[2].value;
    }
    if ((index & (1 << 3)) && bck.line1.data[3].key == key) 
    {
        return bck.line1.data[3].value;
    }
    if ((index & (1 << 4)) && bck.line1.data[4].key == key) 
    {
        return bck.line1.data[4].value;
    }
    if ((index & (1 << 5)) && bck.line1.data[5].key == key) 
    {
        return bck.line1.data[5].value;
    }
    if ((index & (1 << 6)) && bck.line1.data[6].key == key) 
    {
        return bck.line1.data[6].value;
    }
    #else
    /*
    printf("bucketSearch: index: %x, search_key %llx, bck_key: %llx %llx %llx %llx %llx %llx %llx\n", index,
            key, bck.data[0].key, bck.data[1].key, bck.data[2].key, bck.data[3].key, 
            bck.data[4].key, bck.data[5].key, bck.data[6].key, bck.data[7].key);
    */
    if ((index & (1 << 0)) && bck.data[0].key == key) 
    {
        //printf("bucketSearch: 0 %llx\n",bck.data[1].value);
        return bck.data[0].value;
    }
    if ((index & (1 << 1)) && bck.data[1].key == key) 
    {
        //printf("bucketSearch: 1 %llx\n",bck.data[1].value);
        return bck.data[1].value;
    }
    if ((index & (1 << 2)) && bck.data[2].key == key) 
    {
        //printf("bucketSearch: 2 %llx\n",bck.data[1].value);
        return bck.data[2].value;
    }
    if ((index & (1 << 3)) && bck.data[3].key == key) 
    {
        //printf("bucketSearch: 3 %llx\n",bck.data[1].value);
        return bck.data[3].value;
    }
    if ((index & (1 << 4)) && bck.data[4].key == key) 
    {
        //printf("bucketSearch: 4 %llx\n",bck.data[1].value);
        return bck.data[4].value;
    }
    if ((index & (1 << 5)) && bck.data[5].key == key) 
    {
        //printf("bucketSearch: 5 %llx\n",bck.data[1].value);
        return bck.data[5].value;
    }
    if ((index & (1 << 6)) && bck.data[6].key == key) 
    {
        //printf("bucketSearch: 6 %llx\n",bck.data[1].value);
        return bck.data[6].value;
    }
    #endif
    return 0;
}

uint16_t getMask(Bucket &bck, uint64_t hash_key)
{
    __m128i first_fp = _mm_set_epi8(0, 0, 0, 0, 0, getMetadata(bck).fp[10],getMetadata(bck).fp[9],
                getMetadata(bck).fp[8],getMetadata(bck).fp[7],getMetadata(bck).fp[6],
                getMetadata(bck).fp[5],getMetadata(bck).fp[4],getMetadata(bck).fp[3],
                getMetadata(bck).fp[2],getMetadata(bck).fp[1],getMetadata(bck).fp[0]);
    /*
    printf("fp: %x %x %x %x %x %x %x %x %x %x %x\n", 
           getMetadata(bck).fp[0], getMetadata(bck).fp[1], getMetadata(bck).fp[2], 
           getMetadata(bck).fp[3], getMetadata(bck).fp[4], getMetadata(bck).fp[5], 
           getMetadata(bck).fp[6], getMetadata(bck).fp[7], getMetadata(bck).fp[8],
           getMetadata(bck).fp[9], getMetadata(bck).fp[10]);
    */
    uint8_t search_fp = hash_key & 0xff;
    //printf("search_fp: %x\n", search_fp);
    __m128i key_data = _mm_set1_epi8(search_fp);
    __m128i rv_mask = _mm_cmpeq_epi8(first_fp, key_data);
    uint16_t mask = _mm_movemask_epi8(rv_mask);
    //printf("mask: %x\n", mask);
    return mask;
}

//return 0 means can't search
uint64_t hashSearch(Hash *hash, uint64_t key)
{
    uint64_t hash_key = hash_64(key);
    //printf("search: %llx %llx\n",key,hash_key);
    Dir *dir = hash->dir;
    uint64_t index_dir = hash_key >> (KEY_BIT - dir->depth);
    MulSegment *mseg = dir->mseg[index_dir];
    uint64_t index_seg = (hash_key >> (KEY_BIT - mseg->metadata - 1)) & 1;
    Segment *seg = mseg->seg[index_seg];
    uint64_t bucket_index = (hash_key >> FP_BIT) & BUCKET_INDEX_MASK;
    Bucket &first_bucket = seg->_[bucket_index];
    
    uint16_t mask = getMask(first_bucket, hash_key);
    uint16_t first_index = mask & getBitmap(first_bucket) & (~getMembership(first_bucket)) & 0x7f;
    uint16_t first_stash_index = (mask >> 7) & getOverflowBitmap(first_bucket) 
                                & (~getOverflowMembership(first_bucket)) & 0xf;
    uint16_t first_over_index = getOverflowIndex(first_bucket);
    //printf("first index, stash, over: %x %x %x\n", first_index, first_stash_index, first_over_index);
    uint64_t result = bucketSearch(first_index,first_bucket,key);
    //printf("first search: %llx\n", result);
    if(result) return result;

    Bucket &second_bucket = seg->_[(bucket_index + 1)%SEGMENT_SIZE];
    mask = getMask(second_bucket, hash_key);
    uint16_t second_index = mask & getBitmap(second_bucket) & getMembership(second_bucket) & 0x7f;
    uint16_t second_stash_index = (mask >> 7) & getOverflowBitmap(second_bucket) 
                                & getOverflowMembership(second_bucket) & 0xf;
    uint16_t second_over_index = getOverflowIndex(second_bucket);
    //printf("second index, stash, over: %x %x %x\n", second_index, second_stash_index, second_over_index);
    result = bucketSearch(second_index,second_bucket,key);
    //printf("second search: %llx\n", result);
    if(result) return result;

    Stash &stash = seg->stash;
    int i;
    for (i = 0; i < 4; ++i) {
        if ((first_stash_index & (1 << i)) && stash.data[(first_over_index >> (i << 2)) & 0xf].key == key) 
        {
            return stash.data[(first_over_index >> (i << 2)) & 0xf].value;
        }
        if ((second_stash_index & (1 << i)) && stash.data[(second_over_index >> (i << 2)) & 0xf].key == key) 
        {
            return stash.data[(second_over_index >> (i << 2)) & 0xf].value;
        }
    }

    return 0;
}