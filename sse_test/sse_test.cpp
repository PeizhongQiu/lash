
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <mmintrin.h>  //mmx header file//
#include <immintrin.h>
#include <xmmintrin.h> //sse header file(include mmx header file)
#include <emmintrin.h> //sse2 header file(include sse header file)

uint16_t getMask()
{
    __m128i first_fp = _mm_set_epi8(0, 0, 0, 0, 0, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    uint8_t search_fp = 8;
    printf("search_fp: %x\n", search_fp);
    __m128i key_data = _mm_set1_epi8(search_fp);
    __m128i rv_mask = _mm_cmpeq_epi8(first_fp, key_data);
    uint16_t mask = _mm_movemask_epi8(rv_mask);
    printf("mask: %x\n", mask);
    return mask;
}

int main(int argc, char *argv[])
{
    getMask();
    return 0;
}
