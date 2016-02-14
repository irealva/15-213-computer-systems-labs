/* 
 * mm.c
 * Irene Alvarado - ialvarad@andrew.cmu.edu
 * Current performance according to mdriver: 75/100
 *
 * I have implemented an explicit free list allocator with a first fit approach
 * I took quite a bit of base code from the CSAPP book and a. ported it to 
 * 64 bit b. created an explicit free list allocator
 *
 * Key points to know about my explicit list: 
 * -It is demarcated by two NULL pointers. I use these to keep track of where 
 * the beginning and end of the list is. 
 * - I call coalesce at various points: when a heap is extended, when a block
 * is freed, and when a block is split
 * An important optimization is that a block can be split when a block has 
 * space left over greater than the MIN block size 
 *
 * How a free block looks:
 * HEADER (4 bytes) - PREV POINTER (8 bytes) - NEXT POINTER (8 bytes) - 
 * OLDER DATA (various bytes) - FOOTER (4 bytes)
 *
 * How an allocated block looks:
 * HEADER (4 bytes) - DATA (various bytes) - FOOTER (4 bytes) 
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"


/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
//#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */  

// Block has to be at least 24 bytes. 
// 1. For a free block: header (4), prev free (8), next free(8), footer(4). 
// 2/ For an allocated block: header (4), data (16), footer(4)
#define MIN         24      

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)                    

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

//Additional macros to manipulate the free block list
#define NEXT_FREE_BLOCK(bp)(*(void **)(bp + DSIZE))
#define PREV_FREE_BLOCK(bp)(*(void **)(bp))

static char *heap_listp = 0;  /* Pointer to first block */ 
static char *free_p = 0 ; /* Pointer to the free block list */

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void insert_free_block(void *ptr) ;
static void remove_block(void *bp) ; 

#ifdef DEBUG
    static void print_block(void *ptr) ;
#endif

/*
 * mm_init - Called when a new trace starts
 * Initially the heap looks like this and has 32 bytes:
 * PADDING(4) - PROLOGUE HEADER (4) - PREV POINTER (8) - 
 * NEXT POINTER (8) - EPILOGUE HEADER (4) - TAIL (4)
 * The free list pointer is initialized to NULL
 */
int mm_init(void) 
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(8*WSIZE)) == (void *)-1) 
        return -1;
    PUT(heap_listp, 0);                          // Alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(MIN, 1)); // Prologue header
    PUT(heap_listp + (2*WSIZE), 0); // Prev pointer 
    PUT(heap_listp + (4*WSIZE), 0); //Next pointer
    PUT(heap_listp + MIN, PACK(MIN, 1)) ; // Prologue epilogue (footer)
    PUT(heap_listp + MIN + WSIZE, PACK(0,1)) ; // Tail

    #ifdef DEBUG
        mm_checkheap(0) ;
        print_block(heap_listp + MIN + DSIZE) ;
    #endif
    
    heap_listp += (2*WSIZE);                      
    free_p = NULL ;

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    return 0;
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 * Extend heap is called to increase the heap size when either the heap is
 * initialized or the heap is out of free blocks
 * Important to note that the size of the extension is aligned to 8 bytes
 */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    size = ALIGN(size) ;

    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */   
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */   
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 

    bp = coalesce(bp) ; 
    insert_free_block(bp) ;

    mm_checkheap(0) ;

    return bp;                                          
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 * Malloc always allocate a block whose size is a multiple of the alignment.
 * Malloc does not change from the implicit list implementation. The important
 * changes are within place and coalesce functions
 * 
 */
void *malloc(size_t size) 
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      

    if (heap_listp == 0){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)                                          
        asize = 2*DSIZE;                                        
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); 

    asize = MAX(asize, MIN) ; // Make sure alignment is correct for 64 bit

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);                  
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    place(bp, asize); 

    return bp;
} 

/*
 * free - Free a block of previously allocated memory
 * An important change with respect to the implicit list implmentation is that
 * We call coalesce and insert the newly freed block into the free block list
 */
void free(void *bp){
    if (bp == 0) 
        return;

    size_t size = GET_SIZE(HDRP(bp));

    if (heap_listp == 0){
        mm_init();
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    
    bp = coalesce(bp);
    insert_free_block(bp) ; //Insert the free block into the free block list

}

/*
 * Realloc - Change the size of the block by mallocing a new block,
 * copying its data, and freeing the old block.  
 * Realloc does not change much from the implicit list implementation
 */
void *realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;
    size_t asize ;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)                                          
        asize = 2*DSIZE;                                        
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); 
        
    asize = MAX(asize, MIN) ;

    /* Case 1: If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        free(ptr);
        return 0;
    }

    /* Case 2: If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return malloc(size);
    }

    oldsize = GET_SIZE(HDRP(ptr));

    /* Case 3: if old size and new size are the same */
    if(oldsize == asize) {
        return ptr ;
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    free(ptr);

    return newptr;
}

/*
 * Calloc - Allocate the block and set it to zero.
 * Required for mdriver
 */
void *calloc (size_t nmemb, size_t size)
{
  size_t bytes = nmemb * size;
  void *newptr;

  newptr = malloc(bytes);
  memset(newptr, 0, bytes);

  return newptr;
}

/*
 * Coalesce - Join two adjacent free blocks and return a pointer to the 
 * coalesced block
 * This function only executes if the free block list is non empty
 */
 
static void *coalesce(void *bp) 
{
    if(free_p != NULL) {
        size_t prev_alloc ;
        size_t next_alloc ;
        
        prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) ;
        next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))) ;

        size_t size = GET_SIZE(HDRP(bp));

        //Case 1: Left block is free
        if(!prev_alloc && next_alloc) {
            size += GET_SIZE(HDRP(PREV_BLKP(bp)));
            bp = PREV_BLKP(bp);
            remove_block(bp);
            PUT(HDRP(bp), PACK(size, 0));
            PUT(FTRP(bp), PACK(size, 0));
        }
        //Case 2: Right block is free
        else if(prev_alloc && !next_alloc) {
            size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
            remove_block(NEXT_BLKP(bp));
            PUT(HDRP(bp), PACK(size, 0));
            PUT(FTRP(bp), PACK(size, 0));
        }
        //Case 3: Both right and left blocks are free
        else if(!prev_alloc && !next_alloc) {
            size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
                GET_SIZE(HDRP(NEXT_BLKP(bp)));
            remove_block(PREV_BLKP(bp));
            remove_block(NEXT_BLKP(bp));
            bp = PREV_BLKP(bp);
            PUT(HDRP(bp), PACK(size, 0));
            PUT(FTRP(bp), PACK(size, 0));
        }

    }
    
    return bp;
}



/* 
 * Place - Place block of asize bytes at start of free block bp 
 * and split if remainder is at least minimum block size of 24 bytes
 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));   
    asize = asize ;
        
    //If we can split the block we need to make sure we remove the extra block
    // And re-add it to the free block list    
    if ((csize - asize) >= MIN) { 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        remove_block(bp) ; 
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        bp = coalesce(bp) ; // See if we can coalesce block
        insert_free_block(bp) ;
    }
    else { 
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        remove_block(bp) ;
    }
}

/* 
 * Find_fit - Find a fit for a block with asize bytes 
 * In the case of the explicit list implementation, we only search through 
 * the free list with pointer free_p until we find a block big enough to fit
 * asize
 */
static void *find_fit(size_t asize)
{
    void *bp;

    //Iterate through free list until you hit NULL
    for(bp = free_p ; bp != NULL ; bp = NEXT_FREE_BLOCK(bp)) {
        size_t block_size = (size_t) GET_SIZE(HDRP(bp)) ;
        if(asize <= block_size) {
            return bp ;
        }
    }

    return NULL; /* No fit */
}

/* 
 * Insert_free_block - Function for the explicit list implementation. 
 * Inserts a free block at the beginning of the free block list as pointed 
 * to by free_p. 
 */
static void insert_free_block(void *ptr) {
    if(free_p == NULL) { //If the free block list is empty
        free_p = ptr ;
        NEXT_FREE_BLOCK(ptr) = NULL ;
        PREV_FREE_BLOCK(ptr) = NULL ;
    }
    else { //If the free block list is non-empty
        PREV_FREE_BLOCK(free_p) = ptr ;
        PREV_FREE_BLOCK(ptr) = NULL ;
        NEXT_FREE_BLOCK(ptr) = free_p ;
        free_p = ptr ;
    }
}

/* 
 * Remove_block - Function for the explicit list implementation. 
 * Remove a block that has been malloced from the free block list. 
 * This function is a little tricky in that we need to check whether the 
 * block to remove is at the beginning or end of list 
 */
static void remove_block(void *bp)
{
    // Case 1: Block to remove is in the middle of free list
    if (PREV_FREE_BLOCK(bp) != NULL && NEXT_FREE_BLOCK(bp) != NULL) {
        NEXT_FREE_BLOCK(PREV_FREE_BLOCK(bp)) = NEXT_FREE_BLOCK(bp);
        PREV_FREE_BLOCK(NEXT_FREE_BLOCK(bp)) = PREV_FREE_BLOCK(bp);

    }
    // Case 2: Block to remove is at the end of the free list
    else if (PREV_FREE_BLOCK(bp) != NULL && NEXT_FREE_BLOCK(bp) == NULL) {
        NEXT_FREE_BLOCK(PREV_FREE_BLOCK(bp)) = NEXT_FREE_BLOCK(bp);
    }
    // Case 3: Block to remove is the first block and the last one. 
    // Free list is size 1
    else if (PREV_FREE_BLOCK(bp) == NULL && NEXT_FREE_BLOCK(bp) == NULL){
        free_p = NEXT_FREE_BLOCK(bp);
    }
    // Case 4: Block to remove is the first block in the free list
    else if(PREV_FREE_BLOCK(bp) == NULL && NEXT_FREE_BLOCK(bp) != NULL) {
        PREV_FREE_BLOCK(NEXT_FREE_BLOCK(bp)) = NULL ;
        free_p = NEXT_FREE_BLOCK(bp);
    }
}

/*
 * mm_checkheap - Function to check the consistency of the heap
 */
void mm_checkheap(int lineno){
    /*Get gcc to be quiet. */
    lineno = lineno;

    //1. Check that beginning of heap is correct
    if(!((mem_heap_lo()+DSIZE) == heap_listp)) {
        printf("Heap start is NOT correct\n") ;
        exit(0) ;
    }

    //2. Check the prologue block is correct
    int prologue_header_size = GET_SIZE(HDRP(heap_listp)) ;
    int prologue_footer_size = GET_SIZE(FTRP(heap_listp)) ;
    int prologue_header_alloc = GET_ALLOC(HDRP(heap_listp)) ;
    int prologue_footer_alloc = GET_ALLOC(FTRP(heap_listp)) ;

    if(prologue_header_size != MIN && prologue_footer_size != MIN) {
        printf("Prologue size is NOT correct\n") ;
        exit(0) ;
    }
    if(prologue_header_alloc != 1 && prologue_footer_alloc != 1) {
        printf("Prologue alloc NOT set correctly\n") ;
        exit(0) ;
    }

    //3. Check epilogue block is correct
    size_t test = mem_heapsize() ;
    void *epilogue_block = mem_heap_lo() + (test - WSIZE);

    int epilogue_size = GET_SIZE(epilogue_block) ;
    int epilogue_alloc = GET_ALLOC(epilogue_block) ;
    if(epilogue_size != 0) {
        printf("Epilogue size is NOT correct\n") ;
        exit(0) ;
    }
    if(epilogue_alloc != 1) {
        printf("Epilogue alloc is NOT set correctly\n") ;
        exit(0) ;
    }

    //4. Check the free block list
    if(free_p != NULL) {
        if(PREV_FREE_BLOCK(free_p) != NULL) {
            printf("Start of free block list is corrupt\n") ;
            exit(0) ;
        }
    }

    void *bp ;
    for(bp = free_p ; bp != NULL ; bp = NEXT_FREE_BLOCK(bp)) {
        if(NEXT_FREE_BLOCK(bp) != NULL) {
            if(bp != PREV_FREE_BLOCK(NEXT_FREE_BLOCK(bp))) {
                printf("Corrupt prev and next pointers in free list\n") ;
                exit(0) ;
            }
        }

        if(bp < mem_heap_lo() || bp > mem_heap_hi()) {
            printf("A free block is out of bounds\n") ;
            exit(0) ;
        }
    }

    //5. Check all the blocks
    for(bp = mem_heap_lo() + WSIZE ; (bp - (WSIZE - 1)) > mem_heap_hi() ; 
        bp = NEXT_BLKP(bp)) {
        if(GET_SIZE(HDRP(bp)) != GET_SIZE(FTRP(bp))) {
            printf("Header and footer size for a block do not match\n") ;
            exit(0) ;
        }

        if(GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp))) {
            printf("Allocation for header/footer for a block do not match\n") ;
            exit(0) ;
        }

        int size = GET_SIZE(HDRP(bp)) ;
        if(size != ALIGN(size)) {
            printf("Size is not aligned for a block\n") ;
            exit(0) ;
        }
    }
}


#ifdef DEBUG
    /*
     * Print_block - Function used for debugging that prints the contents
     * of a free or allocated block
     */
    static void print_block(void *ptr) {
    int header_alloc = GET_ALLOC(HDRP(ptr)) ;
    int footer_alloc = GET_ALLOC(FTRP(ptr)) ;
    int header_size = GET_SIZE(HDRP(ptr)) ;
    int footer_size = GET_SIZE(FTRP(ptr)) ;

    // If allocated but header size is 0, then a tail block
    if(header_alloc && (header_size == 0)) {
        printf("Tail block %p\n", ptr) ;
        return ;
    }

    // if both header and footer allocatios set to 1, then allocated block
    if(header_alloc && footer_alloc) {
        printf("Allocated block %p -- Header: %d #### Footer: %d\n", 
            ptr, header_size, footer_size) ;
    }
    else {
        printf("Free block %p -- Header: %d #### Footer: %d", 
            ptr, header_size, footer_size) ; 
        printf("-- Prev pointer: %p #### Next pointer: %p\n", 
            PREV_FREE_BLOCK(ptr), NEXT_FREE_BLOCK(ptr)) ;
    }
}
#endif


