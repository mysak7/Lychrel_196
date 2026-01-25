/*
  Copyright © 2011-2013 Romain Dolbeau <romain@dolbeau.org>
  
  This file is part of p196_mpi.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) && defined(_MSC_VER)
#include "XGetopt.h"
#else
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <mpi.h>

#define VERSION_MAJOR 1
#define VERSION_MINOR 9
#define VERSION_EXTRA ".0"

#if defined(_WIN32) && defined(_MSC_VER)
#define inline
#define atoll(a) _atoi64(a)
#define _bswap64(a) _byteswap_uint64(a)
#define ALIGN16 __declspec(align(16))
#define ALIGN32 __declspec(align(32))
#define ALIGN64 __declspec(align(64))
#define percentzd "%Id"
#else
#define percentzd "%zd"
#ifdef __INTEL_COMPILER
#define ALIGN16 __declspec(align(16))
#define ALIGN32 __declspec(align(32))
#define ALIGN64 __declspec(align(64))
#else // assume GCC
#define ALIGN16  __attribute__((aligned(16)))
#define ALIGN32  __attribute__((aligned(32)))
#define ALIGN64  __attribute__((aligned(64)))
#define _bswap64(a) __builtin_bswap64(a)
#endif
#endif

#ifdef ARMNEON
#include <arm_neon.h>
#endif

#if defined(SSE4) || defined(SSSE3) || defined(SSE3)
#include <emmintrin.h>
#if defined(SSE4) || defined(SSSE3)
#include <tmmintrin.h>
#if defined(SSE4)
#include <smmintrin.h>
#endif
#endif
#endif

#if defined(AVX2)
#include <immintrin.h>
#endif

#if defined(MIC)
#include <immintrin.h>
#endif

#ifdef PWR7
#ifdef __IBMC__
#define LVSL_CAST_INT
#define LVSL_CAST_PTR
#else // assume GCC
#include <altivec.h>
#define LVSL_CAST_INT (int)
#define LVSL_CAST_PTR (const unsigned char*)
#endif
#endif

#if defined(MIC)
#define ROUND_VALUE  ~0x003FULL
#define INC_VALUE    64
#else
/* larger could work better, but smaller allows for a smaller difference
   in block size - thus allowing better efficiency at high processes count */
#if defined(AVX2)
#if defined(ALIGN_AVX2) // should be defined, except for many, many processess
#define ROUND_VALUE  ~0x001FULL
#define INC_VALUE    32
#else
#warning "ALIGN_AVX2 is not defined. You probably should define it, unless using a very large number of processes relative to the data size."
#define ROUND_VALUE  ~0x0003ULL
#define INC_VALUE    4
#endif
#else
#define ROUND_VALUE  ~0x000FULL
#define INC_VALUE    16
#endif
#endif

#if !defined(AVX2) && !defined(SSE4) && !defined(SSSE3) && !defined(SSE3)
#ifndef PWR7
#ifndef MIC
#ifndef ARMNEON
#warning "Neither AVX2, SSE4, SSSE3, SSE3 nor PWR7 nor MIC nor ARMNEON has been defined. Are you sure about that?"
#endif
#endif
#endif
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif

#include "isf.h"
#include "mydump.h"

/** So how does this work ?
    We have from digits 0 to full_size-1 inclusive.
    We have mpi_size process working together.
    We start by cutting the data space in nb_blocks = 2 * mpi_size chunks.
    Process 0 will do block 0 and nb_blocks-1 (the first and last one)
    Process 1 will do block 1 and nb_blocks-1 (the second and next-to-last one)
    And so on.
    That way, if all blocks are of equal sizes, then each process is responsible
    for one block *and* the corresponding mirror block.
    At each iteration, everyone compute its own blocks, assuming that there was
    no carry entering those blocks. Then, everyone forwards the carries it has
    produced to the appropriate process (mpi_rank+1 for the first block,
    mpi_rank-1 for the second block). Once received, the carries are "patched"
    into the blocks, and propagated. If a carry is propagated through an entire
    block, the whole communication-and-patch thing is done a second (third, ...)
    times. Then we can start the next iteration.
    Obviously, not all blocks can be of equal size. So we decide to make the last
    block shorter. Consequently, the data & mirror don't line up properly anymore.
    So after each computation, we need to propagate some data so that the process
    can access the mirror data they are not responsible for.
    The ratio amount of data communicated is dependant on the difference in sizes
    between the last block and the other, so we should minimize that ; on the other
    hand, that last block absorb the growth in digits, and at some points it becomes
    as big as the others - and then we need to redistribute work. We don't want that
    to happen too often, so there is some tweaking to be done there.
    Also, the optimal number of processes depends on the size of the problem (which
    varies all the time...): too many processes, and everyone does very little and
    efficiency goes down the drain, we mostly move carries & data around. Too
    little, and kernel efficiency drops because we spill out of caches.
    Parallelism is not easy :-)
*/

/* TODO: FIXME: there's a few cast from int size_t to int, as MPI uses int for
   count of elements and stuff. If we use more than 2^31-1 digits, this will
   not work. */

// #define VERBOSE 1

#ifdef VERBOSE
#define SHOWWHERE() fprintf(stderr, "%d: in %s @ %d\n", mpi_rank, __PRETTY_FUNCTION__, __LINE__)
#else
#define SHOWWHERE() do {} while (0)
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#include <windows.h>

double getTimer(void) {
  static int init = 1 ;
  static double tv0 ;
  __int64 freq;
  __int64 clock;
  double current ;
  QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
  QueryPerformanceCounter( (LARGE_INTEGER *)&clock );
  current = ((double)clock/freq*1000*1000)/1000000.;
  if (!init) {
    tv0 = current;
    init = 1;
  }
  return current - tv0;
}

#else
#include <sys/time.h>
#include <time.h>

double 
getTimer(void)
{
  static int init = 1 ;
  static struct timeval tv0 ;
  if (init) 
  {
    // Remember first call as start of times ; ugly
    gettimeofday(&tv0,NULL) ;
    init=0;
    return 0.0 ; 
  } 
  else 
  {
    struct timeval tv ;
    gettimeofday(&tv,NULL) ;
    return (tv.tv_sec-tv0.tv_sec) + (tv.tv_usec-tv0.tv_usec) * 0.000001 ;
  }
}
#endif

/** max size of the problem
    This could be replaced by dynamic re-allocation to save on memory.
    If you're short on memory, use a smaller values, and when
    you reach the max, recompile and restart from the last dump :-)
*/
#define MAX_SIZE_GLOBAL (1000*1000*1000)

/* most of the optimized code won't work in other bases */
#define base 10

// #define TIMER_COMMS 1
/** it's informative for tweaking ratios only.
*/
typedef struct {
  double tcomm;
  double tcommCarry;
  double tcommAsNeeded;
  double tcommEveryone;
  double tcommResizing;
  double tcommZero;
  double tcommPpalAllReduce;
} all_timers;

/** first element (inclusive) in what to compute in mpi_rank for block #n (0 or 1) */
inline size_t getFirst(int mpi_rank, int mpi_size, size_t full_size, size_t base_size, size_t last_size, size_t n) {
  if (n == 0)
    return mpi_rank * base_size;
  else
    return (mpi_size + mpi_size - (mpi_rank + 1)) * base_size;
}

/** number of elements in what to compute in mpi_rank for block #n (0 or 1) */
inline size_t getSize(int mpi_rank, int mpi_size, size_t full_size, size_t base_size, size_t last_size, size_t n) {
  if (n == 0)
    return base_size;
  else
    if (mpi_rank != 0)
      return base_size;
    else
      return last_size; 
}

/** last element (exclusive) in what to compute in mpi_rank for block #n (0 or 1) */
inline size_t getLast(int mpi_rank, int mpi_size, size_t full_size, size_t base_size, size_t last_size, size_t n) {
  return
    getFirst(mpi_rank, mpi_size, full_size, base_size, last_size, n) + 
    getSize(mpi_rank, mpi_size, full_size, base_size, last_size, n);
}

/* large number of arguments are annoying */
/* really should have used a struct */
#define GET_FIRST(mpi_rank, n)                                          \
  getFirst(mpi_rank, mpi_size, full_size, base_size, last_size, n)

#define GET_SIZE(mpi_rank, n)                                           \
  getSize(mpi_rank, mpi_size, full_size, base_size, last_size, n)

#define GET_LAST(mpi_rank, n)                                           \
  getLast(mpi_rank, mpi_size, full_size, base_size, last_size, n)

/** first element (inclusive) in what is needed in the mirror to compute in mpi_rank for block #n (0 or 1) */
inline size_t getMirrorFirst(int mpi_rank, int mpi_size, size_t full_size, size_t base_size, size_t last_size, size_t n) {
  size_t last = GET_LAST(mpi_rank, n);
  return (full_size - last);
}

/** last element (exclusive) in what is needed in the mirror to compute in mpi_rank for block #n (0 or 1) */
inline size_t getMirrorLast(int mpi_rank, int mpi_size, size_t full_size, size_t base_size, size_t last_size, size_t n) {
  size_t first = GET_FIRST(mpi_rank, n);
  return (full_size - first);
}

/* large number of arguments are annoying */
/* really should have used a struct */
#define GET_MIRROR_FIRST(mpi_rank, n)                                   \
  getMirrorFirst(mpi_rank, mpi_size, full_size, base_size, last_size, n)

#define GET_MIRROR_LAST(mpi_rank, n)                                    \
  getMirrorLast(mpi_rank, mpi_size, full_size, base_size, last_size, n)

#define RESIZING_TAG_0 0x0001
#define RESIZING_TAG_1 0x0002
#define RESIZING_TAG_2 0x0003
#define RESIZING_TAG_3 0x0004
#define CARRY_TAG_0    0x0005
#define CARRY_TAG_1    0x0006
#define ASNEEDED_TAG_0 0x0007
#define ASNEEDED_TAG_1 0x0008

/** communicator to propagate carries among processes ; returns the last carry (i.e. did we just add a digit to the number) */
inline char dadd_inter_process_carry(int mpi_rank, int mpi_size,
                                     size_t full_size,
                                     size_t me_first[2], size_t me_last[2],
                                     size_t base_size, size_t last_size, size_t me_size[2],
                                     char *current, char *next, char carry[2], all_timers *at) {
  size_t i;
  char newcarry[2], finalcarry = 0;
  int rindex = 0;
  int ierr, j;
  MPI_Status status[4];
  MPI_Request request[4];
#ifdef TIMER_COMMS
  double tc0 = getTimer(), tc1;
#endif

  for (j = 0 ; j < 2 ; j++) {
    newcarry[j] = 0;
  }
  
  newcarry[0] = 1;
  while (newcarry[0]) {
    int a,b;
    rindex = 0;
    for (j = 0 ; j < 2 ; j++) {
      newcarry[j] = 0;
    }
    /* block 0 ; send carry to next */
    if (mpi_rank < mpi_size - 1) {
      ierr = MPI_Isend(&carry[0], 1, MPI_CHAR, mpi_rank + 1, CARRY_TAG_0, MPI_COMM_WORLD, &request[rindex]);
      rindex++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Send failed with %d\n", mpi_rank, ierr);
    } else {
      /* send to myself, to block 1 */
      /* done in the recv section */
    }
        
    /* block 1 ; send carry to previous */
    if (mpi_rank > 0) {
      ierr = MPI_Isend(&carry[1], 1, MPI_CHAR, mpi_rank - 1, CARRY_TAG_1, MPI_COMM_WORLD, &request[rindex]);
      rindex++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Send failed with %d\n", mpi_rank, ierr);
    } else {
      finalcarry += carry[1];
    }

    /* block 0 : receive carry from previous */
    if (mpi_rank > 0) {
      ierr = MPI_Irecv(&newcarry[0], 1, MPI_CHAR, mpi_rank - 1, CARRY_TAG_0, MPI_COMM_WORLD, &request[rindex]);
      rindex++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
    }

    /* block 1 : receive carry from next */
    if (mpi_rank < mpi_size - 1) {
      ierr = MPI_Irecv(&newcarry[1], 1, MPI_CHAR, mpi_rank + 1, CARRY_TAG_1, MPI_COMM_WORLD, &request[rindex]);
      rindex++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
    } else {
      newcarry[1] = carry[0];
    }     

    /* wait for all communications (at most 4) */
    if (rindex) {
      ierr = MPI_Waitall(rindex, request, status);
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Waitall failed with %d\n", mpi_rank, ierr);
    }
    carry[0] = newcarry[0];
    carry[1] = newcarry[1];
    for (j = 0 ; j < 2 ; j++) {
      /* propagate the newly received carry in the low-order
         digits, and quit as soon as the carry has been absorbed */
      for (i = me_first[j] ; i < me_last[j] && carry[j]; i++) {
        char result = next[i] + carry[j];
        carry[j] = (result >= base) ? 1 : 0;
        next[i] = carry[j] ? result - base : result;
      }
    }
    /* MPI_MAX on MPI_CHAR is not always supported, use MPI_INT instead */
    a = carry[0] || carry[1];
    /* let everyone know if a carry propagates through an entire block
       and need further propagation. This is almost always not the case
       but we need to check it anyway */
    ierr = MPI_Allreduce(&a, &b, 1, MPI_INT, MPI_LOR, MPI_COMM_WORLD);
    newcarry[0] = b;
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "MPI_Allreduce failed with %d\n", ierr);
    /* at that point, if at least one carry was propagated, do everything again... */
  }
  
  /* let everyone know whether we have a 'final carry', i.e. the number
     of digits just went up by one. */
  ierr = MPI_Bcast(&finalcarry, 1, MPI_CHAR, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

#ifdef TIMER_COMMS
  tc1 = getTimer();
  at->tcomm += tc1 - tc0;
  at->tcommCarry += tc1 - tc0;
#endif
  
  return finalcarry;
}

#if defined(MIC)
inline __m512i _mm512_loaddu_bswap(unsigned char *p, size_t offset) {
  __m512i result;
  __declspec(align(64)) unsigned char temp[64];
  int i;
  for (i = 0 ; i < 64 ; i++) {
    temp[63-i] = p[offset + i];
  }
  result = _mm512_load_epi32((unsigned int*)temp);
  return result;
}
#endif

/** The reverse-and-add funcion. */
inline size_t dadd(int mpi_rank, int mpi_size,
                   size_t full_size,
                   size_t me_first[2], size_t me_last[2],
                   size_t base_size, size_t last_size, size_t me_size[2],
                   char *current, char *next, all_timers *at) {
  size_t i;
  char newcarry = 0, finalcarry = 0;
  int j;
  char carries[2];

  SHOWWHERE();

  for (j = 0 ; j < 2 ; j++)
    carries[j] = 0;
#ifdef TWIN
#warning "AVX2 TWIN is experimental code and not very efficient. You probably should use normal AVX2."
  size_t first[2];
  size_t last[2];
  size_t i1;
  char carry[2];
  first[0] = me_first[0];
  first[1] = me_first[1];
  last[0] = me_last[0];
  last[1] = me_last[1];
  carry[0] = 0;
  carry[1] = 0;
  i = first[0];
  i1 = first[1];
#if defined(AVX2)
#include "p196_mpi_twin_avx2.c"
#endif
  first[0] = i;
  first[1] = i1;
  for (j = 0 ; j < 2 ; j++) {
    for (i = first[j] ; i < last[j] ; i++) {
      char result = current[i] + current[full_size - (i + 1)] + carry[j];
      carry[j] = (result >= base) ? 1 : 0;
      next[i] = carry[j] ? result - base : result;
    }
    carries[j] = carry[j];
  }
#else // TWIN
  for (j = 0 ; j < 2 ; j++)
  {
    char carry = 0;
    size_t first, last;

    first = me_first[j];
    last = me_last[j];

    i = first;
#ifdef ARMNEON
    /* this uses NEON to compute 32 bytes at once */
#include "p196_mpi_neon.c"
#endif // ARMNEON
#if defined(AVX2)
    /* this uses AVX2 to compute 128 bytes at once */
#include "p196_mpi_avx2.c"
#endif // AVX2
#if defined(SSE4) || defined(SSSE3) || defined(SSE3)
    /* this uses SSE4 to compute 64 bytes at once */
#include "p196_mpi_sseX.c"
#endif // SSE*
#ifdef MIC
    /* this uses KNC-VI to compute 128 bytes at once */
#include "p196_mpi_knc.c"
#endif // MIC
#ifdef PWR7
    /* this uses VMX (AltiVec) to compute 64 bytes at once */
#include "p196_mpi_vmx.c"
#endif // PWR7
    /** ultra basic sequential adder.
        complete the computation if needed,
        or actually does it if neither SSE4 or PWR7 was used */
    for ( ; i < last ; i++) {
      char result = current[i] + current[full_size - (i + 1)] + carry;
      carry = (result >= base) ? 1 : 0;
      next[i] = carry ? result - base : result;
    }
    carries[j] = carry;
  }
#endif // TWIN

  /** inter-process carry propagation */
  finalcarry = dadd_inter_process_carry(mpi_rank, mpi_size,
                                        full_size,
                                        me_first, me_last,
                                        base_size, last_size, me_size,
                                        current, next, carries, at);

  /** if there was a final carry, full_size gets bigger by one */
  if (finalcarry)
  {
    next[full_size] = 1;
    full_size++;
  }
  return full_size;
}

inline int ppal(size_t full_size, char* current, size_t me_first[2], size_t me_last[2]) {
  size_t i;
  int abort = 0;
  /* we only do the first block, as the second is in the second half so is tested by us or our neighbour */
  size_t first = me_first[0];
  size_t last  = me_last[0];
  size_t k = full_size - 1;
  for (i = first ; i < last && !abort; ++i) {
    if (current[i] != current[k - i])
      abort = 1;
  }
  return abort;
}

/** Communicator to ensure each block has a valid copies of data it needs
    If base_size == last_size, then block 0 and block 1 are a perfect
    match and no communication is needed ; otherwise, (base_size - last_size)
    bytes are 'off', and need to be sent by one process to the next/previous
*/
int updateasneeded(int mpi_rank, int mpi_size,
                   size_t full_size,
                   size_t me_first[2], size_t me_last[2],
                   size_t base_size, size_t last_size, size_t me_size[2],
                   char *current, char *next, all_timers *at) {
  int ierr;
  MPI_Status status[4];
  MPI_Request request[4];
  int rindex = 0;
#ifdef TIMER_COMMS
  double tc0 = getTimer(), tc1;
#endif

  SHOWWHERE();
  
  /* block 0 send */
  /* everyone but the last sends to the next process */
  if (mpi_rank < (mpi_size - 1)) {
    size_t s1, e1;
    int ss1;
    s1 = GET_MIRROR_FIRST((mpi_rank + 1), 1);
    e1 = GET_MIRROR_LAST((mpi_rank + 1), 1);
    
    if (s1 < GET_FIRST(mpi_rank, 0))
      s1 = GET_FIRST(mpi_rank, 0);
    if (e1 > GET_LAST(mpi_rank, 0))
      e1 = GET_LAST(mpi_rank, 0);
    
    ss1 = (int)(e1 - s1);
    
    ierr = MPI_Isend(current + s1, ss1, MPI_CHAR, mpi_rank + 1, ASNEEDED_TAG_0, MPI_COMM_WORLD, &request[rindex]);
    rindex ++;
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "%d: MPI_Isend failed with %d\n", mpi_rank, ierr);
  }

  /* block 0 receive */
  /* everyone but the first receive from previous process */
  if (mpi_rank > 0) {
    size_t s1, e1;
    int ss1;
    s1 = GET_MIRROR_FIRST(mpi_rank, 1);
    e1 = GET_MIRROR_LAST(mpi_rank, 1);
    
    if (s1 < GET_FIRST((mpi_rank - 1), 0))
      s1 = GET_FIRST((mpi_rank - 1), 0);
    if (e1 > GET_LAST((mpi_rank - 1), 0))
      e1 = GET_LAST((mpi_rank - 1), 0);
    
    ss1 = (int)(e1 - s1);
    
    ierr = MPI_Irecv(current + s1, ss1, MPI_CHAR, mpi_rank - 1, ASNEEDED_TAG_0, MPI_COMM_WORLD, &request[rindex]);
    rindex ++;
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
  }

  /* block 1 send */
  /* everyone but the first send to the previous process */
  if (mpi_rank > 0) {
    size_t s1, e1;
    int ss1;
    s1 = GET_MIRROR_FIRST((mpi_rank - 1), 0);
    e1 = GET_MIRROR_LAST((mpi_rank - 1), 0);
    
    if (s1 < GET_FIRST(mpi_rank, 1))
      s1 = GET_FIRST(mpi_rank, 1);
    if (e1 > GET_LAST(mpi_rank, 1))
      e1 = GET_LAST(mpi_rank, 1);
    
    ss1 = (int)(e1 - s1);
    
    ierr = MPI_Isend(current + s1, ss1, MPI_CHAR, mpi_rank - 1, ASNEEDED_TAG_1, MPI_COMM_WORLD, &request[rindex]);
    rindex ++;
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "%d: MPI_Isend failed with %d\n", mpi_rank, ierr);
  }
  
  /* block 1 receive */
  /* everyone but the last receive from next process */
  if (mpi_rank < (mpi_size - 1)) {
    size_t s1, e1;
    int ss1;
    s1 = GET_MIRROR_FIRST(mpi_rank, 0);
    e1 = GET_MIRROR_LAST(mpi_rank, 0);
    
    if (s1 < GET_FIRST((mpi_rank + 1), 1))
      s1 = GET_FIRST((mpi_rank + 1), 1);
    if (e1 > GET_LAST((mpi_rank + 1), 1))
      e1 = GET_LAST((mpi_rank + 1), 1);
    
    ss1 = (int)(e1 - s1);
    
    ierr = MPI_Irecv(current + s1, ss1, MPI_CHAR, mpi_rank + 1, ASNEEDED_TAG_1, MPI_COMM_WORLD, &request[rindex]);
    rindex ++;
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
  }
  
  ierr = MPI_Waitall(rindex, request, status);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "%d: MPI_Waitall failed with %d\n", mpi_rank, ierr);
  
#ifdef TIMER_COMMS
  tc1 = getTimer();
  at->tcomm += tc1 - tc0;
  at->tcommAsNeeded += tc1 - tc0;
#endif
  
  SHOWWHERE();

  return 0;
}

/** function to 'guess' a good size for base_size and last_size.
    this is the most likely function to need cluster-specific tweaking */
inline void compute_sizes(int mpi_rank, int mpi_size,
                          size_t full_size, size_t nb_blocks, size_t *fbase_size, size_t *flast_size) {
  size_t base_size, last_size;
  double ratio;
  base_size = (size_t)((double)(full_size)/((double)nb_blocks)) & ROUND_VALUE;
  /* heuristic ; 25k seems ok after improving resizing */
  /* in practice, for a large number of processes, the number of
     blocks and the rounding requirements limit the 'smallness':
     for 1600 blocks (800 processes), each rounding increment changes
     last_size by more than 100k... */
  ratio = 25000. / (double)base_size;
  if (ratio < 0.001)
    ratio = 0.001;
  if (ratio > 0.10)
    ratio = 0.10;
  base_size = (size_t)((double)(full_size)/((double)nb_blocks-ratio)) & ROUND_VALUE;
  last_size = full_size - (nb_blocks-1)*base_size;
  if (mpi_rank == 0) {
    fprintf(stderr, "%d: first guess is " percentzd " / " percentzd "\n", mpi_rank, base_size, last_size);
  }
  if ((ssize_t)last_size <= 0) {
    if (mpi_rank == 0) {
      fprintf(stderr, "%d: last_size < 0 : couldn't redistribute data, aborting\n", mpi_rank);
    }
    MPI_Abort(MPI_COMM_WORLD, 4);
    exit(-4);
  }
  if ((ssize_t)base_size <= 0) {
    if (mpi_rank == 0) {
      fprintf(stderr, "%d: base_size < 0 : couldn't redistribute data, aborting\n", mpi_rank);
    }
    MPI_Abort(MPI_COMM_WORLD, 6);
    exit(-6);
  }
  while (base_size <= last_size) {
    base_size += INC_VALUE;
    last_size = full_size - (nb_blocks-1)*base_size;
    if (mpi_rank == 0) {
      fprintf(stderr, "%d: current guess is " percentzd " / " percentzd "\n", mpi_rank, base_size, last_size);
    }
    if ((ssize_t)last_size <= 0) {
      if (mpi_rank == 0) {
        fprintf(stderr, "%d: last_size < 0 : couldn't redistribute data, aborting\n", mpi_rank);
      }
      MPI_Abort(MPI_COMM_WORLD, 5);
      exit(-5);
    }
    if ((ssize_t)base_size <= 0) {
      if (mpi_rank == 0) {
        fprintf(stderr, "%d: base_size < 0 : couldn't redistribute data, aborting\n", mpi_rank);
      }
      MPI_Abort(MPI_COMM_WORLD, 7);
      exit(-7);
    }
  }
  *fbase_size = base_size;
  *flast_size = last_size;
}

/** communicator the ensure everyone has the complete data
    this is time-consuming, shouldn't be called too often.
    In theory, it is never needed ; only process 0 needs
    everything (for dumps), while re-sizing should only
    need much less communications (see below) */
int updateeveryone(int mpi_rank, int mpi_size,
                   size_t full_size,
                   size_t me_first[2], size_t me_last[2],
                   size_t base_size, size_t last_size, size_t me_size[2],
                   char *current, char *next, all_timers *at) {
#if defined(_WIN32) && defined(_MSC_VER)
  int *sendcnts = (int*)malloc(mpi_size * sizeof(int));
  int *sdispls = (int*)malloc(mpi_size * sizeof(int));
  int *recvcnts = (int*)malloc(mpi_size * sizeof(int));
  int *rdispls = (int*)malloc(mpi_size * sizeof(int));
#else
  int sendcnts[mpi_size];
  int sdispls[mpi_size];
  int recvcnts[mpi_size];
  int rdispls[mpi_size];
#endif
  int i, ierr, j;
#ifdef TIMER_COMMS
  double tc0 = getTimer(), tc1;
#endif

  SHOWWHERE();

  for (j = 0 ; j < 2 ; j++) {
    /* send the same thing to everyone */
    for (i = 0 ; i < mpi_size ; i++) {
      sendcnts[i] = (int)me_size[j];
      sdispls[i] = (int)me_first[j];
    }
    /* receive evryone else's block */
    for (i = 0 ; i < mpi_size; i++) {
      recvcnts[i] = (int)GET_SIZE(i, j);
      rdispls[i] = (int)GET_FIRST(i, j);
    }
    
#ifdef VERBOSE
    for (i = 0 ; i < mpi_size ; i++) {
      fprintf(stderr, "%d: sending %d to %d @ %d\n", mpi_rank, sendcnts[i], sdispls[i], i);
      fprintf(stderr, "%d: receiving %d from %d @ %d\n", mpi_rank, recvcnts[i], rdispls[i], i);
    }
#endif
    
    ierr = MPI_Alltoallv (current,
                          sendcnts, 
                          sdispls, 
                          MPI_CHAR, 
                          current, 
                          recvcnts, 
                          rdispls, 
                          MPI_CHAR,
                          MPI_COMM_WORLD);
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "%d: MPI_Alltoallv failed with %d\n", mpi_rank, ierr);
  }

#ifdef TIMER_COMMS
  tc1 = getTimer();
  at->tcomm += tc1 - tc0;
  at->tcommEveryone += tc1 - tc0;
#endif

  SHOWWHERE();

#if defined(_WIN32) && defined(_MSC_VER)
  free(sendcnts);
  free(sdispls);
  free(recvcnts);
  free(rdispls);
#endif
  
  return 0;
}

/** communicator to ensure every process has all the date it will need after
    a resizing operation. This is likely to transfer too much data, but resizing
    are infrequent. Also, unless updateeveyrone, it only requires local
    communications with the two neighbours.
    Basically: both blocks are sent left and right (for direct & mirror). */
int updateforresizing(int mpi_rank, int mpi_size,
        size_t full_size,
        size_t me_first[2], size_t me_last[2],
        size_t base_size, size_t last_size, size_t me_size[2],
        char *current, char *next, all_timers *at) {
    int ierr = 0;
    MPI_Status status[8];
    MPI_Request request[8];
    int rindex = 0;
#ifdef TIMER_COMMS
    double tc0 = getTimer(), tc1;
#endif
    /* send my first block to the left */
    if (mpi_rank > 0) {
      ierr = MPI_Isend(current + me_first[0],
                      (int)me_size[0], MPI_CHAR, mpi_rank - 1, RESIZING_TAG_0,
                       MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Isend failed with %d\n", mpi_rank, ierr);
    }
    /* send my first block to the right */
    if (mpi_rank < (mpi_size - 1)) {
      ierr = MPI_Isend(current + me_first[0],
                      (int)me_size[0], MPI_CHAR, mpi_rank + 1, RESIZING_TAG_1,
                      MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Isend failed with %d\n", mpi_rank, ierr);
    }
    /* send my second block to the left */
    if (mpi_rank > 0) {
      ierr = MPI_Isend(current + me_first[1],
                       (int)me_size[1], MPI_CHAR, mpi_rank - 1, RESIZING_TAG_2,
                       MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Isend failed with %d\n", mpi_rank, ierr); 
    }
    /* send my second block to the right */
    if (mpi_rank < (mpi_size - 1)) {
      ierr = MPI_Isend(current + me_first[1],
                       (int)me_size[1], MPI_CHAR, mpi_rank + 1, RESIZING_TAG_3,
                       MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Isend failed with %d\n", mpi_rank, ierr);
    }
    
    /* receive */
    if (mpi_rank < (mpi_size - 1)) {
      ierr = MPI_Irecv(current + GET_FIRST((mpi_rank + 1), 0),
                       (int)GET_SIZE((mpi_rank + 1), 0), MPI_CHAR, mpi_rank + 1, RESIZING_TAG_0,
                       MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
    }
    if (mpi_rank > 0) {
      ierr = MPI_Irecv(current + GET_FIRST((mpi_rank - 1), 0),
                      (int)GET_SIZE((mpi_rank - 1), 0), MPI_CHAR, mpi_rank - 1, RESIZING_TAG_1,
                      MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
    }
    if (mpi_rank < (mpi_size - 1)) {
      ierr = MPI_Irecv(current + GET_FIRST((mpi_rank + 1), 1),
                      (int)GET_SIZE((mpi_rank + 1), 1), MPI_CHAR, mpi_rank + 1, RESIZING_TAG_2,
                      MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
    }
    if (mpi_rank > 0) {
      ierr = MPI_Irecv(current + GET_FIRST((mpi_rank - 1), 1),
                      (int)GET_SIZE((mpi_rank - 1), 1), MPI_CHAR, mpi_rank - 1, RESIZING_TAG_3,
                      MPI_COMM_WORLD, &request[rindex]);
      rindex ++;
      if (ierr != MPI_SUCCESS)
        fprintf(stderr, "%d: MPI_Irecv failed with %d\n", mpi_rank, ierr);
    }
  
    ierr = MPI_Waitall(rindex, request, status);
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "%d: MPI_Waitall failed with %d\n", mpi_rank, ierr);
    
#ifdef TIMER_COMMS
    tc1 = getTimer();
    at->tcomm += tc1 - tc0;
    at->tcommResizing += tc1 - tc0;
#endif

    return 0;
}

/** communicator the ensure process #0 has the complete data,
    used before a dump */
int updatezero(int mpi_rank, int mpi_size,
               size_t full_size,
               size_t me_first[2], size_t me_last[2],
               size_t base_size, size_t last_size, size_t me_size[2],
               char *current, char *next, all_timers *at) {
  int sendcnts;
  int sdispls;
#if defined(_WIN32) && defined(_MSC_VER)
  int *recvcnts = (int*)malloc(mpi_size * sizeof(int));
  int *rdispls = (int*)malloc(mpi_size * sizeof(int));
#ifndef MPI_ALLOW_ALIAS
  char *mpi_dealias = (char*)malloc(base_size * sizeof(char));
#endif
#else
  int recvcnts[mpi_size];
  int rdispls[mpi_size];
#ifndef MPI_ALLOW_ALIAS
  char mpi_dealias[base_size];
#endif
#endif
  int i, ierr, j;
#ifdef TIMER_COMMS
  double tc0 = getTimer(), tc1;
#endif

  SHOWWHERE();
  
  for (j = 0 ; j < 2 ; j++) {
    sendcnts = (int)me_size[j];
    sdispls = (int)me_first[j];
    /* recv only significant for #0 */
    for (i = 0 ; i < mpi_size; i++) {
      recvcnts[i] = (int)GET_SIZE(i, j);
      rdispls[i] = (int)GET_FIRST(i, j);
    }

#ifndef MPI_ALLOW_ALIAS
    memcpy(mpi_dealias, current + sdispls, sendcnts);
#endif
    
    ierr = MPI_Gatherv(
#ifndef MPI_ALLOW_ALIAS
                       mpi_dealias,
#else
                       current + sdispls,
#endif
                       sendcnts,
                       MPI_CHAR, 
                       current, 
                       recvcnts, 
                       rdispls, 
                       MPI_CHAR,
                       0,
                       MPI_COMM_WORLD);
    if (ierr != MPI_SUCCESS)
      fprintf(stderr, "%d: MPI_Gatherv failed with %d\n", mpi_rank, ierr);
  }

#ifdef TIMER_COMMS
  tc1 = getTimer();
  at->tcomm += tc1 - tc0;
  at->tcommZero += tc1 - tc0;
#endif

  SHOWWHERE();

#if defined(_WIN32) && defined(_MSC_VER)
  free(recvcnts);
  free(rdispls);
#ifndef MPI_ALLOW_ALIAS
  free(mpi_dealias);
#endif
#endif
  
  return 0;
}

int main(int argc, char **argv) {
  /** mpi_rank & mpi_size are classics ; who we are & how many processes is there in the group */
  int mpi_rank, mpi_size;
  /** MPI error */
  int ierr;
  /** allocation pointers */
  char *b0, *b1;
  /** start_size: the size of the problem from the data file;
      base_size: the size of all subblocks except the last one;
      last_size: the size of the last subblock (<= base_size);
      start: the initial value we're working on (196 :-);
      step: the current iteration;
      firststep: the first iteration in this run;
      nbdumpstep: how often to dump the data;
      full_size: current size of the data (# of digits);
      displaystep: when did we last display something;
      maxstep: how many iterations to do before stopping (relative)
      stopstep : stop at that iteration
  */
  size_t start_size, base_size, last_size, start, step = 0, firststep, nbdumpstep = 1000000, full_size, displaystep, maxstep = 50000, stopstep = 0;
  /** dump every time the #of digits is a multiple of this */
  size_t dumpdigits = 1000000;
  /** stop at this number of digits */
  size_t maxdigits = 0;
  /** my blocks */
  size_t me_size[2], me_first[2], me_last[2];
  /** pointer to data currently valid */
  char *current;
  /** pointer to the next data */
  char *next;
  /** various timers */
  double tt0, tt1, tt1d, tt1o, tt0b;
  /** counters for statistics */
  long long td = 0, dtd = 0;
  /** filename to open */
  char restart[512];
  /** for command-line options */
  int opt;
  /** local test for palindromicity */
  int test = 0;
  /** global test for palindromicity */
  int gtest;
  /** how many subblocks */
  size_t nb_blocks;
  /** do we use ISF dumps */
  int use_isf = 0;
  /** timers */
  all_timers at;
  int res;

#ifdef TIMER_COMMS
  memset(&at, 0, sizeof(all_timers));
#endif

  restart[0] = '\0';
  
  MPI_Init(&argc, &argv);
  ierr = MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Comm_rank failed with %d\n", ierr);
  ierr =  MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Comm_size failed with %d\n", ierr);
  
#ifdef VERBOSE
  fprintf(stderr, "%d: of %d\n", mpi_rank, mpi_size);
#endif
  
  if (mpi_rank == 0)
  while ((opt = getopt(argc, argv, "i:m:d:D:M:L:F")) != EOF) {
    switch (opt) {
    case 'm':
      maxstep = atoll(optarg);
      break;
    case 'L':
      stopstep = atoll(optarg);
      break;
    case 'd':
      nbdumpstep = atoll(optarg);
      break;
    case 'D':
      dumpdigits = atoll(optarg);
      break;
    case 'i':
      sprintf(restart, "%s", optarg);
      break;
    case 'M':
      maxdigits = atoll(optarg);
      break;
    case 'F':
      use_isf = 1;
      break;
    default: /* '?' */
      if (mpi_rank == 0) {
        fprintf (stderr, "Usage: %s -i <restartfile> [-m <nb>] [-d <nb>] [-D <nb>] [-M <nb>] [-L <nb>] [-F]\n", argv[0]);
        fprintf (stderr, "*         -i: restart file (mandatory)\n");
        fprintf (stderr, "*         -m: max. number of iterations [relative to start] (0: unlimited)\n");
        fprintf (stderr, "*         -L: max. number of iterations [absolute value] (0: unlimited)\n");
        fprintf (stderr, "*         -M: max. number of digits (0: unlimited)\n");
        fprintf (stderr, "*         -d: nb iterations between dump (0: disable) \n");
        fprintf (stderr, "*         -D: multiple of that # of digits will be dumped (0: disable)\n");
        fprintf (stderr, "*         -F: dump ISF files\n");
        fprintf (stderr, "I don't understand:\n\t");
        for (ierr = 0 ; ierr < argc ; ierr++) {
          fprintf(stderr, "%s ", argv[ierr]);
        }
        fprintf(stderr, "\n");
      }
      exit (0);
    }
  }
  
  if (mpi_rank == 0) {
    fprintf(stderr, "This is p196_mpi (%s) version %d.%d%s for %s using %d processes\n", argv[0],
            VERSION_MAJOR,
            VERSION_MINOR,
            VERSION_EXTRA,
#if defined(AVX2)
            "AVX2"
#elif defined(SSE4)
            "SSE4"
#elif defined(SSSE3)
            "SSSE3"
#elif defined(SSE3)
            "SSE3"
#elif defined(PWR7)
            "PWR7"
#elif defined(ARMNEON)
            "NEON"
#else
            "C"
#endif
            , mpi_size
            );
    fprintf(stderr, "%d: maxstep = " percentzd " ; stopstep = " percentzd " ; nbdumpstep = " percentzd " ; dumpdigits = " percentzd " ; input = '%s' ; use_isf = %s\n",
            mpi_rank, maxstep, stopstep, nbdumpstep, dumpdigits, restart, use_isf ? "true" : "false");
  }

  if (mpi_rank == 0) {
    if (!restart[0]) {
      fprintf(stderr, "%d: I need an input file\n", mpi_rank);
      exit(-2);
    }
  }

#ifdef MIC_MALLOC
  b0 = _mm_malloc(MAX_SIZE_GLOBAL, 64);
  b1 = _mm_malloc(MAX_SIZE_GLOBAL, 64);
  memset(b0, 0, MAX_SIZE_GLOBAL);
  memset(b1, 0, MAX_SIZE_GLOBAL);
#else
/*  b0 = (char*)calloc(MAX_SIZE_GLOBAL, 1); */
/*  b1 = (char*)calloc(MAX_SIZE_GLOBAL, 1); */
  res = posix_memalign((void**)&b0, 32, MAX_SIZE_GLOBAL);
  if (res) {
    fprintf(stderr, "posix_memalign() returned %d\n", res);
    exit(-1);
  }
  res = posix_memalign((void**)&b1, 32, MAX_SIZE_GLOBAL);
  if (res) {
    fprintf(stderr, "posix_memalign() returned %d\n", res);
    exit(-1);
  }
  memset(b0, 0, MAX_SIZE_GLOBAL);
  memset(b1, 0, MAX_SIZE_GLOBAL);
#endif
  if (!b0 || !b1) {
    fprintf(stderr, "%d: CALLOC FAILED!\n", mpi_rank);
  }
#ifdef VERBOSE
  fprintf(stderr, "%d: %p (%p), %p (%p)\n", mpi_rank,
          b0, b0 + MAX_SIZE_GLOBAL, b1, b1 + MAX_SIZE_GLOBAL);
#endif

  if (mpi_rank == 0) {
    if (strstr(restart, ".isf") != NULL) {
      int err = read_isf(restart, &start_size, &start, &step, b0);
      if (err) {
        fprintf(stderr, "Reading ISF file failed\n");
        exit(-3);
      }
    } else {
      int err = read_dump(restart, &start_size, &start, &step, b0);
      if (err) {
        fprintf(stderr, "Reading dump file failed\n");
        exit(-3);
      }
    }
    firststep = step;
  }

  if (mpi_rank == 0) {
    fprintf(stderr, "Broadcasting parameters...\n");
  }
  if (mpi_rank == 0) {
    fprintf(stderr, "\t start_size (" percentzd ")\n", start_size);
  }
  ierr = MPI_Bcast(&start_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t start (" percentzd ")\n", start);
  }
  ierr = MPI_Bcast(&start, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t step (" percentzd ")\n", step);
  }
  ierr = MPI_Bcast(&step, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t firststep (" percentzd ")\n", firststep);
  }
  ierr = MPI_Bcast(&firststep, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t maxstep (" percentzd ")\n", maxstep);
  }
  ierr = MPI_Bcast(&maxstep, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t stopstep (" percentzd ")\n", stopstep);
  }
  ierr = MPI_Bcast(&stopstep, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t nbdumpstep (" percentzd ")\n", nbdumpstep);
  }
  ierr = MPI_Bcast(&nbdumpstep, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t dumpdigits (" percentzd ")\n", dumpdigits);
  }
  ierr = MPI_Bcast(&dumpdigits, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (mpi_rank == 0) {
    fprintf(stderr, "\t maxdigits (" percentzd ")\n", maxdigits);
  }
  ierr = MPI_Bcast(&maxdigits, 1, MPI_LONG, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  if (start_size >= MAX_SIZE_GLOBAL) {
    if (mpi_rank == 0)
      fprintf(stderr, "file has grown beyond control, aborting (" percentzd " for %d / " percentzd ")\n", start_size, mpi_size, (size_t)MAX_SIZE_GLOBAL);
    exit(-1);
  }

  nb_blocks = 2 * mpi_size;
 
  full_size = start_size;
  compute_sizes(mpi_rank, mpi_size, full_size, nb_blocks, &base_size, &last_size);

  me_size[0]  = GET_SIZE(mpi_rank, 0);
  me_first[0] = GET_FIRST(mpi_rank, 0);
  me_last[0]  = GET_LAST(mpi_rank, 0);

  me_size[1]  = GET_SIZE(mpi_rank, 1);
  me_first[1] = GET_FIRST(mpi_rank, 1);
  me_last[1]  = GET_LAST(mpi_rank, 1);
 
  if (mpi_rank == 0)
    fprintf(stderr, "%d: decided upon " percentzd ", " percentzd " for " percentzd " ; I have from " percentzd " to " percentzd " (" percentzd ") and from " percentzd " to " percentzd " (" percentzd ")\n", mpi_rank,
            base_size, last_size, full_size,
            me_first[0], me_last[0], me_size[0],
            me_first[1], me_last[1], me_size[1]);
  
    ierr = MPI_Bcast(b0, (int)full_size, MPI_CHAR, 0, MPI_COMM_WORLD);
  if (ierr != MPI_SUCCESS)
    fprintf(stderr, "MPI_Bcast failed with %d\n", ierr);

  SHOWWHERE();
  
  /* at this point, everyone has the full starting value and know who is responsible for what */

  current = b0;
  next = b1;
  {
    tt0 = getTimer();
    tt1 = tt1d = tt1o = tt0;
    tt0b = tt0;
    displaystep = firststep;
    do {
      do {
        test = ppal(full_size, current, me_first, me_last);
        {
#ifdef TIMER_COMMS
          double tc0 = getTimer(), tc1;
#endif
          ierr = MPI_Allreduce(&test, &gtest, 1, MPI_INT, MPI_LOR, MPI_COMM_WORLD);
          if (ierr != MPI_SUCCESS)
            fprintf(stderr, "MPI_Allreduce failed with %d\n", ierr);
#ifdef TIMER_COMMS
          tc1 = getTimer();
          at.tcomm += tc1 - tc0;
          at.tcommPpalAllReduce += tc1 - tc0;
#endif
        }
        if (gtest) {
          char *p;
          size_t temp;
          size_t oldfull_size = full_size;
          temp = dadd(mpi_rank, mpi_size,
                      full_size, me_first, me_last,
                      base_size, last_size, me_size,
                      current, next, &at);
          last_size += temp - full_size;
          if (mpi_rank == 0) {
            me_size[1] = last_size;
            me_last[1] = me_first[1] + me_size[1];
          }
          full_size = temp;

          p = current;
          current = next;
          next = p;
          td += full_size;
          dtd += full_size;

          updateasneeded(mpi_rank, mpi_size,
                         full_size, me_first, me_last,
                         base_size, last_size, me_size,
                         current, next, &at);
          step++;

          SHOWWHERE();

          tt1 = getTimer();
          if ((mpi_rank == 0)/*  || (mpi_rank == (mpi_size/2)) */) {
            if ((tt1 - tt1o) > 1.) {
              fprintf(stderr, "%d: %lf: " percentzd " ; " percentzd " / " percentzd " (%lf / %lf iter/s) (%le / %le d/s) (%llu)",
                      mpi_rank,
                      tt1 - tt0, step, (step-firststep), (step - displaystep),
                      (double)(step-firststep)/(tt1-tt0), (double)(step-displaystep)/(tt1-tt0b),
                      (double)td/(tt1-tt0), (double)dtd/(tt1-tt0b),
                      (unsigned long long)full_size);
#ifdef TIMER_COMMS
              fprintf(stderr, " comm: %lf (C: %lf, AN: %lf, E: %lf, R: %lf, Z: %lf, PAR: %lf",
                      at.tcomm,
                      at.tcommCarry,
                      at.tcommAsNeeded,
                      at.tcommEveryone,
                      at.tcommResizing,
                      at.tcommZero,
                      at.tcommPpalAllReduce
                      );
              at.tcomm = 0.;
              at.tcommCarry = 0.;
              at.tcommAsNeeded = 0.;
              at.tcommEveryone = 0.;
              at.tcommResizing = 0.;
              at.tcommZero = 0.;
              at.tcommPpalAllReduce = 0.;
#endif
              fprintf(stderr, "\n");
              tt1o = tt1;
              tt0b = tt1;
              dtd = 0;
              displaystep = step;
            }
          }

          SHOWWHERE();
        
          if (((nbdumpstep > 0) && !((step - firststep) % nbdumpstep)) ||
              ((dumpdigits > 0) && !(full_size % dumpdigits)))
          {
#ifdef VERBOSE
            fprintf(stderr, "%d: preparing for dump\n", mpi_rank);
#endif
            updatezero(mpi_rank, mpi_size,
                       full_size, me_first, me_last,
                       base_size, last_size, me_size,
                       current, next, &at);
            if (mpi_rank == 0) {
              char buf[96];
              int res;
              sprintf (buf, "dump." percentzd "." percentzd "", start, step);
              res = write_dump(buf, full_size, start, step, current);
              if (res) {
                fprintf(stderr, "%d: oups, dump failed @ " percentzd "\n", mpi_rank, step);
              }
              if (use_isf) {
                sprintf(buf, "dump." percentzd "." percentzd ".isf", start, step);
                res = write_isf(buf, full_size, start, step, current);
                if (res) {
                  fprintf(stderr, "%d: oups, ISF dump failed @ " percentzd "\n", mpi_rank, step);
                }
              }
              fprintf(stderr, "%d: done dump @ " percentzd "\n", mpi_rank, step);
              tt1d = tt1;
            }
          }
        } else {
          if (mpi_rank == 0)
            fprintf(stderr, "%d: Found palindrome at iteration " percentzd "\n", mpi_rank, step);
          updateeveryone(mpi_rank, mpi_size,
                         full_size, me_first, me_last,
                         base_size, last_size, me_size,
                         current, next, &at);
          if (mpi_rank == 0) {
            char buf[96];
            int res;
            sprintf (buf, "dump." percentzd "." percentzd ".PALINDROME", start, step);
            res = write_dump(buf, full_size, start, step, current);
            if (res) {
              fprintf(stderr, "%d: oups, dump failed @ " percentzd "\n", mpi_rank, step);
            }
            if (use_isf) {
              sprintf (buf, "dump." percentzd "." percentzd ".isf", start, step);
              res = write_isf(buf, full_size, start, step, current);
              if (res) {
                fprintf(stderr, "%d: oups, ISF dump failed @ " percentzd "\n", mpi_rank, step);
              }
            }
            fprintf(stderr, "%d: done dump @ " percentzd "\n", mpi_rank, step);
          }
        }
      } while ((last_size < base_size) &&
               ((maxstep == 0) || ((step - firststep) < maxstep)) &&
               ((stopstep == 0) || (step < stopstep)) &&
               ((maxdigits == 0) || (maxdigits > full_size))  &&
               gtest &&
               (full_size < MAX_SIZE_GLOBAL));
      if (last_size == base_size) {
        if (mpi_rank == 0) {
          fprintf(stderr, "%d: last_size (" percentzd ") has reached base_size, need to recompute sizes\n", mpi_rank, last_size);
        }
        updateforresizing(mpi_rank, mpi_size,
                          full_size, me_first, me_last,
                          base_size, last_size, me_size,
                          current, next, &at);
        compute_sizes(mpi_rank, mpi_size, full_size, nb_blocks, &base_size, &last_size);
        
        me_size[0]  = GET_SIZE(mpi_rank, 0);
        me_first[0] = GET_FIRST(mpi_rank, 0);
        me_last[0]  = GET_LAST(mpi_rank, 0);
        
        me_size[1]  = GET_SIZE(mpi_rank, 1);
        me_first[1] = GET_FIRST(mpi_rank, 1);
        me_last[1]  = GET_LAST(mpi_rank, 1);
        
        if (mpi_rank == 0)
          fprintf(stderr, "%d: decided upon " percentzd ", " percentzd " for " percentzd " ; I have from " percentzd " to " percentzd " (" percentzd ") and from " percentzd " to " percentzd " (" percentzd ")\n", mpi_rank,
                  base_size, last_size, full_size,
                  me_first[0], me_last[0], me_size[0],
                  me_first[1], me_last[1], me_size[1]);
      }
    } while ((last_size < base_size) &&
             ((maxstep == 0) || ((step - firststep) < maxstep)) &&
             ((stopstep == 0) || (step < stopstep)) &&
             ((maxdigits == 0) || (maxdigits > full_size))  &&
             gtest &&
             (full_size < MAX_SIZE_GLOBAL));
  }
  
  updateeveryone(mpi_rank, mpi_size,
                 full_size, me_first, me_last,
                 base_size, last_size, me_size,
                 current, next, &at);
  
  if (mpi_rank == 0) {
    char buf[96];
    int res;
    sprintf (buf, "dump." percentzd "." percentzd "", start, step);
    res = write_dump(buf, full_size, start, step, current);
    if (res) {
      fprintf(stderr, "%d: oups, dump failed @ " percentzd "\n", mpi_rank, step);
    } 
    if (use_isf) {
      sprintf (buf, "dump." percentzd "." percentzd ".isf", start, step);
      res = write_isf(buf, full_size, start, step, current);
      if (res) {
        fprintf(stderr, "%d: oups, ISF dump failed @ " percentzd "\n", mpi_rank, step);
      }
    }
    fprintf(stderr, "%d: done dump @ " percentzd "\n", mpi_rank, step);
  }
  
  if (mpi_rank == 0)
    fprintf(stderr, "%d: I'am finished (last_size = " percentzd ", base_size = " percentzd ", full_size = " percentzd ") (firststep = " percentzd ", maxstep = " percentzd ", stopstep = " percentzd ", step = " percentzd ")\n",
            mpi_rank,
            last_size, base_size, full_size,
            firststep, maxstep, stopstep, step);
  
  MPI_Finalize();
  return 0;
}
