#include "hash.h"

void mfence()
{
    asm volatile("mfence" ::
                     : "memory");
}
int main(int argc, char *argv[])
{

    int test_item_number = atoi(argv[1]);
    printf("This test insert number is :%d\n", test_item_number);

    Hash hash;

    hashInit(&hash, 4);
    long long *vector;
    vector = (long long *)malloc(sizeof(long long) * test_item_number);
    long long i;
    for (i = 0; i < test_item_number; ++i)
    {
        vector[i] = i;
    }

    srand(time(NULL));
    for (i = 0; i < test_item_number; ++i)
    {
        long long a = rand() % test_item_number, b = rand() % test_item_number;
        long long k = vector[a];
        vector[a] = vector[b];
        vector[b] = k;
    }
    struct timeval start, end;
    long long time_consumption = 0;
    mfence();
    gettimeofday(&start, NULL);
    for (i = 0; i < test_item_number; ++i)
    {
        hashInsert(&hash, vector[i], i+1);
    }
    gettimeofday(&end, NULL);
    mfence();
    time_consumption = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
    printf(" of insert is %lld\n", time_consumption);
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
    printf(" of search is %lld\n", time_consumption);
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
    printf("time_consumption of search is %lld\n", time_consumption);
    printf("re-search IOPS is %lf\n", test_item_number * 1000000.0 / time_consumption);
    return 0;
}