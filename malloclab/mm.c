/*
A Dynamic Memory Allocator(DMA) is  the coding construct that manages the heap portion of a processes virtual memory
a DMA is a tool to allocate memory at runtime. It is usually beneficial to do this since certain variables are not determined until runtime
usually due to user input. Lets say a user wants to store a list of N items(Item[N]), but we only allocated space for M items(Item[M]), where M < N
Our program will need to be recompiled in order for it to work with that data set. or imagine the opposite situation where a program allocated 
Item[N] and the user only provides M items, where M > N, we end up wasting a ton of memory. A DMA allocates the memory need at runtime so we know exactly
how much space we need as the user input of N items becomes known. this is achieved in the C standard library with void *malloc(size_t size) and void free(void *ptr),
functions that allocate a SIZEamount of bytes and free it (when it is no longer needed), respectively. Thanks to virtual addressing the DMA  only has to keep track of
which portions of ITS heap are allocated and free. DMA's also strive to maximize memory utilization(which can be hindered by fragmentation) and throughput 
(which can be hindered by overhead and the algorithm used to find a free block of memory).

Our implementation uses the following to achieve those goals

A) Keep track of Allocated and Free blocks - Implicit Free List DMA implementation 
We maintain an implicit free list that embeds data in the blocks themselves that enable us to distinguish between allocated and free blocks.
Every block has a word-sized header(4 bytes) where the upper 29 bits are used to encode the block size and since the lower 3 bits will never be used for sizing
(due to the fact that every block size must be 8-byte aligned and therefore must be a multiple of 8 - so we'll never need to use bits that provide more precision 
than 8 dec = 1000 bin, (2)*8 = 16 dec = 10000 bin ... etc. notice how the last 3 bits will always be unused since they are NOT a multiple of 8 b/c they are all less than 8).
So we use those bits to convey if a block is allocated or not, where 1 means it is allocated for the LSB and 0 means it is free. 
The rest of the block after the header is the payload which would contain the actual data and possibly some padding to meet that 8-byte alignment requirement.
At the end of the block is also a footer which is identical to the header this enables Bi-directional Coalescing discussed in C).
VISUAL:
       Block Of Memory                                         Header/Footer Block Zoom in: (1/0 LSB - 1 Allocated, 0 Free)
_____________________________________________                   ________________________________
|   |                                 |  |   |                 |   |  |  |   |     |     |     |
| H |    P  A   Y   L   O   A   D     | P| F |                 |1/0| 0| 0|   |BYTE2|BYTE3|BYTE4|
|___|_________________________________|__| __|                 |___|__|__|___|_____|_____|_____|
    |        Size                     |                        |bitbitbit|# of Bytes/Blocksize |
H = Header                                                     |   BYTE 1   |                  |
P = Padding
F = Footer

B) Search Mechanism to find a Free Block  - First Fit Heap Search(IMPLEMENTATION DETAILS AT METHOD)
We use a first fit heap search algorithm to find free blocks to allocate - it simply searches the heap from the beginning until it find an appropriate free block.
it has the advantage that it leaves large free blocks towards the end of the heap so it scores some points with reducing fragmentation. A disadvantage is that the 
start of the list becomes littered with allocated and freed blocks so the search time for a larger free block takes longer. In my opinion it achieves a good balance of 
memory utilization and speed(througput) for an implicit list implementation. the alternative would be Next Fit or Best Fit.

C) Policies to maximize memory utilization - Immediate  Bi-directioanl Coalescing(IMPLEMENTATION DETAILS AT METHOD)
Coalescing is the act of joining adjacent free blocks into one larger free block. The goal is to be aware of the size of our contiguous free blocks in order to more optimally allocate.
We coalesce after every free operation(this is why it is immediate) to make sure adjacent free blocks become one. the footer block enables us to check if the previous block is free

                                           -Immediate Splitting(IMPLEMENTATION DETAILS IN METHOD "PLACE")
whenever we allocate we always(immediate) split the block if the free block has more space then we need. this also maximizes memory utiiization by decreasing internal fragmentation.
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




/* Basic constants and macros from the book */

#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<8) /* Extend heap by this amount (bytes) */

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

static char* firstBlock = 0; //ptr to the first block in the list - starts at the prologue block

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*A heap checker that checks for the invariants of our Dynamic Memory Allocator
as it is an Implicit Free List Implementation with bidirectional coalescing we
will check if each block is 8-Byte aligned, the header and footer matches, if
there are any adjacent free blocks. In addition we will check for the special 
edge case blocks of the prologue and epilogue blocks that was provided by the book
as a clever trick to avoid repeated testing for the beginning and end of heap
the prologue block should always have the form of a header, footer and no payload
the Header and Footer should have the value 0x009 - this gives the block a size of 8
which is accurate and allows appropriate traversal and a LSB of 1 so it is not coalesced
and remains a border for the heap. As for the Epilogue block it acts as the border for
the end of the heap and signals its existence with having a size of 0 which is solely to 
signal to processes manipulating the heap where the end is. It too has a LSB of 1 to avoid 
coalescing*/
int mm_check(void) {
    /*check the prologue header - make sure every time it has the form 0x009 which is 1001 in bin*/
    unsigned int* pHeader = (unsigned int*)HDRP(firstBlock);
    unsigned int* pFooter = (unsigned int*)FTRP(firstBlock);
    if (*pHeader != *pFooter || GET_SIZE(pHeader) != 8 || !GET_ALLOC(pHeader)) {
        printf("Prologue Header violates heap invariant - prologue is of form \n________________\n|               |\n|%x       %x |  \n", GET(HDRP(firstBlock)), GET(FTRP(firstBlock)));
        return 0;//error
    }
    /*check all the blocks in the heap - are they 8-byte aligned? does the header and footer match? 
    are their any contiguous free blocks? is of the form 0xXXX9 OR 0xXXX8*/
    void* bp;
    //note the iteration through the heap implicitly checks if the epilogue block's size is set to 0 by making it the exit condition
    for (bp = firstBlock; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (GET_SIZE(HDRP(bp)) % 8 != 0) {
            printf("not 8-byte aligned, the size is %d", GET_SIZE(HDRP(bp)));
                return 0;
        }
        if (GET(HDRP(bp)) != GET(FTRP(bp))) {
            printf("header != footer: The header is %x\nThe footer is %x", GET(HDRP(bp)), GET(FTRP(bp)));
                return 0;
        }
        if (GET_SIZE(HDRP(NEXT_BLKP(bp))) > 0 && !GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
            printf("contiguous free blocks - violation of immediate Bi-directional coalescing invariant");
        }
    }
    /*check if the epilogue block's LSB is set to 1 as it should if not it violates invariant*/
    if (!GET_ALLOC(HDRP(bp))) {
        printf("epilogue block messed up");
        return 0;
    }
    return 1;//all invariants remain

}

/* Helper functions from the book ch 9 */


/*
place the requested block at the beginning of the free block, splitting only if
the size of the remainder would equal or exceed the minimum block size.
*/
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));//get the size of the free block
    size_t remainder = csize - asize;//get the extra space in # of bytes that is not needed to store requested block: Freeblock Size - Allocation size
    
    //ensures the remainder of free slots is big enough to be its own free slot
    if (remainder >= (2 * DSIZE)) {//16 bytes - 2 for hdr, ftr and something else for free space which needs to be at least 8 to align it
        //allocate the requested block
        PUT(HDRP(bp), PACK(asize, 1));//set its header
        PUT(FTRP(bp), PACK(asize, 1));//setits footer

        //SPLITTING - put us in position to split - right after the allocated block
        bp = NEXT_BLKP(bp);
        //The remainder of unneeded space becomes a free block
        PUT(HDRP(bp), PACK(remainder, 0));
        PUT(FTRP(bp), PACK(remainder, 0));
    }
    else {
        //no need to split - not enough free bytes left over - just allocate it to the free block
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

//find_fit from book Ch9 
static void* find_fit(size_t asize)
{
/* First-fit search */
    void* bp;//pointer that will iterate through the heap

    /*start at the beginning and go from block to block until we find a free block that has the appropriate size
      or until we reach the epilogue block*/
    for (bp = firstBlock; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }
    //reached epilogue block - just return NULL - there is no kosher location for this requested block size
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

    if (prev_alloc && next_alloc) {    /* Case 1 -> both blocks are allocated no need to coalesce */
        return bp;
    }

    else if (prev_alloc && !next_alloc) {    /* Case 2 -> the next block is free - we will combine them */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));//(size of this free block) + (size of next free block) = (size of coalesced free block)
        PUT(HDRP(bp), PACK(size, 0));
        PUT (FTRP(bp), PACK(size,0));
    }

    /* Whenever we coalesce with a previous block we need to maintain the invariant that 
    the pointer points to the start of a block - so it can find the header and footer 
    - otherwise it will be left in the middle of a free block effectively making all 
    the above macros useless as they do pointer arithemtic with the assumption that bp is at the start of a block */
    else if (!prev_alloc && next_alloc) {    /* Case 3 -> the previous block is free - we will combine them */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));//same as in case 2
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);//<-- INVARIANT MAINTAINED
    }

    else {     /* Case 4 *///same
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
        GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);//<-- INVARIANT MAINTAINED
    }
    return bp;
}

/*
The extend_heap function is invoked in two different circumstances: (1) when the heap is initialized and
(2) when mm_malloc is unable to find a suitable fit. To maintain alignment, extend_heap rounds up the
requested size to the nearest multiple of 2 words (8 bytes) and then requests the additional heap space 
from the memory system(lines 132-134).
*/

static void *extend_heap(size_t words)//WILL BE INITIAL HEAP SIZE
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    //mem_sbrk returns a pointer to the beginning of the newly added on heap
    if ((long)(bp = mem_sbrk(size)) == -1){
        return NULL;
    }
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0)); /* Free block header *///we destroy extra prologue and make it into header in first call
    PUT(FTRP(bp), PACK(size, 0)); /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(bp);//this will turn the old epilogue block into the header or even further back if their is more free space behind epilogue
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
    //mm_check();
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
    if (size <= DSIZE){//no point in allocating a 8-byte block - only room for header and footer - where's the payload? make it at least 16 - bytes
        asize = 2*DSIZE;
    }
    else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);//pad it
    }

    /* Search the free list for a fit */
    (bp = find_fit(asize));
    if (bp != NULL) {
        place(bp, asize);
        //mm_check();
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    bp = extend_heap(extendsize/WSIZE);
    if (bp == NULL){
        return NULL;
    }
    place(bp, asize);

    //mm_check();
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
    PUT(HDRP(ptr), PACK(size, 0));//designate this block as free by setting LSB to 0
    PUT(FTRP(ptr), PACK(size, 0));//same
    coalesce(ptr);
    //mm_check();
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













