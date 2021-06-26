#include "hash.h"
#include <time.h>
#include <sys/time.h>
void mfence()
{
    asm volatile("mfence" ::
                     : "memory");
}
int main(int argc, char *argv[])
{

    uint64_t test_item_number = atoi(argv[1]);
    printf("This test insert number is :%llu\n", test_item_number);

    Hash hash;

    hashInit(&hash, 4);
    uint64_t *vector;
    vector = (uint64_t *)malloc(sizeof(uint64_t) * test_item_number);
    uint64_t i;
    for (i = 0; i < test_item_number; ++i)
    {
        vector[i] = i;
    }

    srand(time(NULL));
    for (i = 0; i < test_item_number; ++i)
    {
        uint64_t a = rand() % test_item_number, b = rand() % test_item_number;
        uint64_t k = vector[a];
        vector[a] = vector[b];
        vector[b] = k;
    }
    struct timeval start, end;
    uint64_t time_consumption = 0;
    mfence();
    gettimeofday(&start, NULL);
    for (i = 0; i < test_item_number; ++i)
    {
        hashInsert(&hash, vector[i], i+1);
    }
    gettimeofday(&end, NULL);
    mfence();
    time_consumption = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    printf(" of insert is %llu\n", time_consumption);
    printf("insert IOPS is %lf\n", test_item_number * 1000000.0 / time_consumption);

    mfence();
    gettimeofday(&start, NULL);
    for (i = 0; i < test_item_number; ++i)
    {
        hashSearch(&hash, vector[i]);
    }
    gettimeofday(&end, NULL);
    mfence();
    time_consumption = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    printf(" of search is %llu\n", time_consumption);
    printf("search IOPS is %lf\n", test_item_number * 1000000.0 / time_consumption);

    mfence();
    gettimeofday(&start, NULL);
    for (i = 0; i < test_item_number; ++i)
    {
        hashSearch(&hash, vector[i]);
    }
    gettimeofday(&end, NULL);
    mfence();
    time_consumption = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    printf("time_consumption of search is %llu\n", time_consumption);
    printf("re-search IOPS is %lf\n", test_item_number * 1000000.0 / time_consumption);
    return 0;
}