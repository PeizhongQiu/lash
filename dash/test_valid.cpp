#include "hash.h"
#include <time.h>
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
    for (i = 0; i < test_item_number; ++i)
    {
        //v[0] = i % 255;
        printf("key:%d insert...\n", vector[i]);
        hashInsert(&hash, vector[i], i+1);
        printf("key:%d insert success\n", vector[i]);
        int j;
        for (j = 0; j < i; ++j)
        {
            uint64_t p = hashSearch(&hash, vector[j]);
            if (p != (j+1))
            {
                printf("error in insert %d %d!\n", vector[j], j);
                return -1;
            }
        }
    }
    printf("insert success!!!\n");
    return 0;
}
