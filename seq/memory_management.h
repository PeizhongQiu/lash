#ifndef LASH_MEM_H
#define LASH_MEM_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "hash.h"

Segment *getNvmBlock(int type);
#endif
