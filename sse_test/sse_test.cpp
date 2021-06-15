
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <mmintrin.h>  //mmx header file//
#include <immintrin.h>
#include <xmmintrin.h> //sse header file(include mmx header file)
#include <emmintrin.h> //sse2 header file(include sse header file)

typedef struct TEST
{
    uint8_t fp[11];
    uint64_t others;
} TEST;

int main(int argc, char *argv[])
{
    TEST test = {{0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0}, 0x123456789abcdef};
    __m128i m1 = _mm_loadu_si128((__m128i *)test.fp);
    uint16_t *val = (uint16_t*) &m1;
    printf("Numerical: %x %x %x %x %x %x %x %x \n", 
           val[0], val[1], val[2], val[3], val[4], val[5], 
           val[6], val[7]);
    int32_t mask = _mm_movemask_epi8(m1);
    __m128i m2 = _mm_set_epi8(test.fp[0],test.fp[1],test.fp[2],test.fp[3],
                                test.fp[4],test.fp[5],test.fp[6],test.fp[7],
                                test.fp[8],test.fp[9],test.fp[10],0,0,0,0,0);
    val = (uint16_t*) &m2;
    printf("Numerical: %x %x %x %x %x %x %x %x \n", 
           val[0], val[1], val[2], val[3], val[4], val[5], 
           val[6], val[7]);                            
    //__m128i m3 = _mm_cmpgt_epi8(m1, m2);
    printf("%x\n", mask);
    return 0;
}
