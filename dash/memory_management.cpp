#include "memory_management.h"
#define PATH "/mypmemfs/ALLOC_FILE"

unsigned long long malloc_num = 0;

void *add_pmalloc(size_t size)
{
    size_t mapped_len;
    char path[100];
    sprintf(path, "%s%llu", PATH, malloc_num);

    void *pmemaddr;
    int is_pmem;
    /* create a pmem file and memory map it */
    if ((pmemaddr = pmem_map_file(path, size, PMEM_FILE_CREATE,
                                  0666, &mapped_len, &is_pmem)) == NULL)
    {
        return NULL;
    }
    ++malloc_num;

    return (void *)pmemaddr;
}


Segment *getNvmBlock(int type)
{
    Segment *newBlock = (Segment *)add_pmalloc(sizeof(Segment));
    if (!newBlock)
    {
        printf("newBlock creation fails: nvm\n");
        exit(1);
    }

    return newBlock;
}
