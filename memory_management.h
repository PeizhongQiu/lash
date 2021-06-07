#ifndef MEM_H
#define MEM_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "hash.h"

Segment *getNvmBlock(int type);
#endif
