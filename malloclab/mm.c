/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Michael and Noah",
    /* First member's full name */
    "Michael Edelman",
    /* First member's email address */
    "mdedelma@mail.yu.edu",
    /* Second member's full name (leave blank if none) */
    "Noah Shafron",
    /* Second member's email address (leave blank if none) */
    "shafron@mail.yu.edu"
};


            /******************HINTS FROM HANDOUT*******************
             
 * Use the mdriver -f option. During initial development, using tiny trace files will simplify 
 * debugging and testing. We have included two such trace files (short1, 2-bal.rep) that you can use for
 * initial debugging.
 
 * Use the mdriver -v and -V options.The -v option will give you a detailed summary for each trace file. 
 * The -V will also indicate when each trace file is read, which will help you isolate errors.
  
 * Compile with gcc -g and use a debugger. A debugger will help you isolate and identify out 
 * of bounds memory references.
 
 * Understand every line of the malloc implementation in the textbook.The textbook has a detailed example 
 * of a simple allocator based on an implicit free list. Use this is a point of departure. Don’t start
 * working on your allocator until you understand everything about the simple implicit list allocator.
  
 * Encapsulate your pointer arithmetic in C preprocessor macros. Pointer arithmetic in memory managers
 *  is confusing and error-prone because of all the casting that is necessary. You can reduce the complexity 
 * significantly by writing macros for your pointer operations. See the text for examples
 
 * Do your implementation in stages.The first 9 traces contain requests to malloc and free. The last 2 
 * traces contain requests for realloc, malloc, and free. We recommend that you start by getting your 
 * malloc and free routines working correctly and efficiently on the first 9 traces. 
 * Only then should you turn your attention to the realloc implementation. For starters, build realloc on
 * top of your existingmalloc and free implementations. But to get really good performance, you will need 
 * to build a stand-alone realloc.
  
 * Use a profiler. You may find the gprof tool helpful for optimizing performance
 
 * Start early! It is possible to write an efficient malloc package with a few pages of code. 
 * However, we can guarantee that it will be some of the most difficult and sophisticated code 
 * you have written so far in your career. So start early, and good luck!
*/


/* Private global variables */
static char *mem_heap; /* Points to first byte of heap */
static char *mem_brk; /* Points to last byte of heap plus 1 */
static char *mem_max_addr; /* Max legal heap addr plus 1*/


/* Basic constants and macros from the book */

#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (* (unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *) (bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))


static char *firstBlock = 0; //ptr to the first block in the list

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


/* Helper functions from the book ch 9 */


/*
place the requested block at the beginning of the free block, splitting only if
the size of the remainder would equal or exceed the minimum block size.
*/
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t remainder = csize - asize;
    //ensures the remainder of free slots is big enough to be its own free slot

    if (remainder >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(remainder, 0));
        PUT(FTRP(bp), PACK(remainder, 0));
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}
//find_fit from book Ch9 
static void *find_fit(size_t asize)
{
    /* First-fit search */
    void *bp;

    for (bp = firstBlock; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <=GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }
    return NULL; /* No fit */
    
}


/*
An application frees a previously allocated block by calling the mm_free function (Figure 9.46), which
frees the requested block ( bp ) and then merges adjacent free blocks using the boundary-tags
coalescing technique described in Section 9.9.11 

The code in the coalesce helper function is a straightforward implementation of the four cases outlined
in Figure 9.40 . There is one somewhat subtle aspect. The free list format we have chosen—with its
prologue and epilogue blocks that are always marked as allocated—allows us to ignore the potentially
troublesome edge conditions where the requested block bp is at the beginning or end of the heap.
Without these special blocks, the code would be messier, more error prone, and slower because we
would have to check for these rare edge conditions on each and every free request.
*/

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {    /* Case 1 */
        return bp;
    }

    else if (prev_alloc && !next_alloc) {    /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT (FTRP(bp), PACK(size,0));
    }

    else if (!prev_alloc && next_alloc) {    /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else {     /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
        GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    return bp;
}

/*
The extend_heap function is invoked in two different circumstances: (1) when the heap is initialized and
(2) when mm_malloc is unable to find a suitable fit. To maintain alignment, extend_heap rounds up the
requested size to the nearest multiple of 2 words (8 bytes) and then requests the additional heap space 
from the memory system(lines 132-134).
*/

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1){
        return NULL;
    }

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0)); /* Free block header */
    PUT(FTRP(bp), PACK(size, 0)); /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

    /* End of book helper functions */



/* 
 * mm_init - initialize the malloc package.

mm_init: Before calling mm_malloc mm_realloc or mm_free, the application program 
(i.e.,the trace-driven driver program that you will use to evaluate your implementation) 
calls mm_init to perform any necessary initializations, such as allocating the initial heap area. 
The return value shouldbe -1 if there was a problem in performing the initialization, 0 otherwise.
*/

/*Before calling mm_malloc or mm_free , the application must initialize the heap by calling the mm_init
function

The mm_init function gets four words from the memory system and initializes them to create the empty
free list (lines 110-117). It then calls the extend_heap function, which extends the heap by
CHUNKSIZE bytes and creates the initial free block. At this point, the allocator is initialized and ready to
accept allocate and free requests from the application.
*/

int mm_init(void)
{
    firstBlock = mem_sbrk(4 * WSIZE);
    /* Create the initial empty heap */
    if (firstBlock == (void *)-1){
        return -1;
    }
    PUT(firstBlock, 0); /* Alignment padding */
    PUT(firstBlock + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(firstBlock + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(firstBlock + (3*WSIZE), PACK(0, 1)); /* Epilogue header */
    firstBlock += (2*WSIZE);

    /* Extend the empty heap with a free block of CHUMSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL){
        return -1;
    }
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.

mm_malloc:The mm_malloc routine returns a pointer to an allocated block payload of at least size bytes. 
The entire allocated block should lie within the heap region and should not overlap with any other 
allocated chunk.

We will be comparing your implementation to the version of malloc supplied in the 
standard C library(libc). Since the libc malloc always returns payload pointers that are aligned to 8 bytes,
 your malloc implementation should do likewise and always return 8-byte aligned pointers.
*/

void *mm_malloc(size_t size)
{
    size_t asize; /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;
    

    /* Ignore spurious requests */
    if (size == 0){
        return NULL;
    }

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE){
        asize = 2*DSIZE;
    }
    else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    }

    /* Search the free list for a fit */
    (bp = find_fit(asize));
    if (bp != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    bp = extend_heap(extendsize/WSIZE);
    if (bp == NULL){
        return NULL;
    }
    place(bp, asize);
    return bp;
    
}

/*
 * mm_free - Freeing a block does nothing.

mmfree:The mm_free routine frees the block pointed to by ptr. It returns nothing. 
This routine is only guaranteed to work when the 
passed pointer (ptr) was returned by an earlier call to 
mm_malloc or mm_realloc and has not yet been freed
*/

void mm_free(void *ptr)
{
    
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);

    return;
}



/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free

mmrealloc:The mm_realloc routine returns a pointer to an allocated region of at least 
size bytes with the following constraints.
    –if ptr is NULL, the call is equivalent to mm_malloc(size);
    –if size is equal to zero, the call is equivalent to mm_free(ptr)
    –if ptr is not NULL, it must have been returned by an earlier call to mm_malloc or mm_realloc.
    
The call to mm_realloc changes the size of the memory block pointed to by ptr(the old block) to size bytes 
and returns the address of the new block. Notice that the address of the new block might be the same as the 
old block, or it might be different, depending on your implementation, the amount of internal fragmentation 
in the old block, and the size of the realloc request.The contents of the new block are the same as those of 
the old ptr block, up to the minimum of the old and new sizes. Everything else is uninitialized. 

For example, if the old block is 8 bytes and the new block is 12 bytes, then the first 8 bytes of the new 
block are identical to the first 8 bytes of the old block and the last 4 bytes are uninitialized. 
Similarly, if the old block is 8 byte sand the new block is 4 bytes, then the contents of the new block are 
identical to the first 4 bytes of the old block.
*/

void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL){
      return NULL;
    }
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize){
        copySize = size;
        memcpy(newptr, oldptr, copySize);
        mm_free(oldptr);
        return newptr;
    }
}













