#include "hash.h"
#include <time.h>
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
    for (i = 0; i < test_item_number; ++i)
    {
        //v[0] = i % 255;
        printf("key:%llu insert...\n", vector[i]);
        hashInsert(&hash, vector[i], i+1);
        printf("key:%llu insert success\n", vector[i]);
        uint64_t j;
        for (j = 0; j < i; ++j)
        {
            uint64_t p = hashSearch(&hash, vector[j]);
            if (p != (j+1))
            {
                printf("error in insert %llu %llu!\n", vector[j], j);
                return -1;
            }
        }
    }
    printf("insert success!!!\n");
    return 0;
}
