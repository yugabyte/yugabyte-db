/*-------------------------------------------------------------------------
 *
 * generation.c
 *	  Generational allocator definitions.
 *
 * Generation is a custom MemoryContext implementation designed for cases of
 * chunks with similar lifespan.
 *
 * Portions Copyright (c) 2017-2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/utils/mmgr/generation.c
 *
 *
 *	This memory context is based on the assumption that the chunks are freed
 *	roughly in the same order as they were allocated (FIFO), or in groups with
 *	similar lifespan (generations - hence the name of the context). This is
 *	typical for various queue-like use cases, i.e. when tuples are constructed,
 *	processed and then thrown away.
 *
 *	The memory context uses a very simple approach to free space management.
 *	Instead of a complex global freelist, each block tracks a number
 *	of allocated and freed chunks.  The block is classed as empty when the
 *	number of free chunks is equal to the number of allocated chunks.  When
 *	this occurs, instead of freeing the block, we try to "recycle" it, i.e.
 *	reuse it for new allocations.  This is done by setting the block in the
 *	context's 'freeblock' field.  If the freeblock field is already occupied
 *	by another free block we simply return the newly empty block to malloc.
 *
 *	This approach to free blocks requires fewer malloc/free calls for truly
 *	first allocated, first free'd allocation patterns.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "lib/ilist.h"
#include "port/pg_bitutils.h"
#include "utils/memdebug.h"
#include "utils/memutils.h"

#define Generation_CONTEXTSZ	MAXALIGN(sizeof(GenerationContext))
#define Generation_BLOCKHDRSZ	MAXALIGN(sizeof(GenerationBlock))
#define Generation_CHUNKHDRSZ	sizeof(GenerationChunk)

#define Generation_CHUNK_FRACTION	8

typedef struct GenerationBlock GenerationBlock; /* forward reference */
typedef struct GenerationChunk GenerationChunk;

typedef void *GenerationPointer;

/*
 * GenerationContext is a simple memory context not reusing allocated chunks,
 * and freeing blocks once all chunks are freed.
 */
typedef struct GenerationContext
{
	MemoryContextData header;	/* Standard memory-context fields */

	/* Generational context parameters */
	Size		initBlockSize;	/* initial block size */
	Size		maxBlockSize;	/* maximum block size */
	Size		nextBlockSize;	/* next block size to allocate */
	Size		allocChunkLimit;	/* effective chunk size limit */

	GenerationBlock *block;		/* current (most recently allocated) block, or
								 * NULL if we've just freed the most recent
								 * block */
	GenerationBlock *freeblock; /* pointer to a block that's being recycled,
								 * or NULL if there's no such block. */
	GenerationBlock *keeper;	/* keep this block over resets */
	dlist_head	blocks;			/* list of blocks */
} GenerationContext;

/*
 * GenerationBlock
 *		GenerationBlock is the unit of memory that is obtained by generation.c
 *		from malloc().  It contains zero or more GenerationChunks, which are
 *		the units requested by palloc() and freed by pfree().  GenerationChunks
 *		cannot be returned to malloc() individually, instead pfree()
 *		updates the free counter of the block and when all chunks in a block
 *		are free the whole block can be returned to malloc().
 *
 *		GenerationBlock is the header data for a block --- the usable space
 *		within the block begins at the next alignment boundary.
 */
struct GenerationBlock
{
	dlist_node	node;			/* doubly-linked list of blocks */
	Size		blksize;		/* allocated size of this block */
	int			nchunks;		/* number of chunks in the block */
	int			nfree;			/* number of free chunks */
	char	   *freeptr;		/* start of free space in this block */
	char	   *endptr;			/* end of space in this block */
};

/*
 * GenerationChunk
 *		The prefix of each piece of memory in a GenerationBlock
 *
 * Note: to meet the memory context APIs, the payload area of the chunk must
 * be maxaligned, and the "context" link must be immediately adjacent to the
 * payload area (cf. GetMemoryChunkContext).  We simplify matters for this
 * module by requiring sizeof(GenerationChunk) to be maxaligned, and then
 * we can ensure things work by adding any required alignment padding before
 * the pointer fields.  There is a static assertion below that the alignment
 * is done correctly.
 */
struct GenerationChunk
{
	/* size is always the size of the usable space in the chunk */
	Size		size;
#ifdef MEMORY_CONTEXT_CHECKING
	/* when debugging memory usage, also store actual requested size */
	/* this is zero in a free chunk */
	Size		requested_size;

#define GENERATIONCHUNK_RAWSIZE  (SIZEOF_SIZE_T * 2 + SIZEOF_VOID_P * 2)
#else
#define GENERATIONCHUNK_RAWSIZE  (SIZEOF_SIZE_T + SIZEOF_VOID_P * 2)
#endif							/* MEMORY_CONTEXT_CHECKING */

	/* ensure proper alignment by adding padding if needed */
#if (GENERATIONCHUNK_RAWSIZE % MAXIMUM_ALIGNOF) != 0
	char		padding[MAXIMUM_ALIGNOF - GENERATIONCHUNK_RAWSIZE % MAXIMUM_ALIGNOF];
#endif

	GenerationBlock *block;		/* block owning this chunk */
	GenerationContext *context; /* owning context, or NULL if freed chunk */
	/* there must not be any padding to reach a MAXALIGN boundary here! */
};

/*
 * Only the "context" field should be accessed outside this module.
 * We keep the rest of an allocated chunk's header marked NOACCESS when using
 * valgrind.  But note that freed chunk headers are kept accessible, for
 * simplicity.
 */
#define GENERATIONCHUNK_PRIVATE_LEN	offsetof(GenerationChunk, context)

/*
 * GenerationIsValid
 *		True iff set is valid allocation set.
 */
#define GenerationIsValid(set) PointerIsValid(set)

#define GenerationPointerGetChunk(ptr) \
	((GenerationChunk *)(((char *)(ptr)) - Generation_CHUNKHDRSZ))
#define GenerationChunkGetPointer(chk) \
	((GenerationPointer *)(((char *)(chk)) + Generation_CHUNKHDRSZ))

/* Inlined helper functions */
static inline void GenerationBlockInit(GenerationBlock *block, Size blksize);
static inline bool GenerationBlockIsEmpty(GenerationBlock *block);
static inline void GenerationBlockMarkEmpty(GenerationBlock *block);
static inline Size GenerationBlockFreeBytes(GenerationBlock *block);
static inline void GenerationBlockFree(GenerationContext *set,
									   GenerationBlock *block);

/*
 * These functions implement the MemoryContext API for Generation contexts.
 */
static void *GenerationAlloc(MemoryContext context, Size size);
static void GenerationFree(MemoryContext context, void *pointer);
static void *GenerationRealloc(MemoryContext context, void *pointer, Size size);
static void GenerationReset(MemoryContext context);
static void GenerationDelete(MemoryContext context);
static Size GenerationGetChunkSpace(MemoryContext context, void *pointer);
static bool GenerationIsEmpty(MemoryContext context);
static void GenerationStats(MemoryContext context,
							MemoryStatsPrintFunc printfunc, void *passthru,
							MemoryContextCounters *totals,
							bool print_to_stderr);

#ifdef MEMORY_CONTEXT_CHECKING
static void GenerationCheck(MemoryContext context);
#endif

/*
 * This is the virtual function table for Generation contexts.
 */
static const MemoryContextMethods GenerationMethods = {
	GenerationAlloc,
	GenerationFree,
	GenerationRealloc,
	GenerationReset,
	GenerationDelete,
	GenerationGetChunkSpace,
	GenerationIsEmpty,
	GenerationStats
#ifdef MEMORY_CONTEXT_CHECKING
	,GenerationCheck
#endif
};


/*
 * Public routines
 */


/*
 * GenerationContextCreate
 *		Create a new Generation context.
 *
 * parent: parent context, or NULL if top-level context
 * name: name of context (must be statically allocated)
 * minContextSize: minimum context size
 * initBlockSize: initial allocation block size
 * maxBlockSize: maximum allocation block size
 */
MemoryContext
GenerationContextCreate(MemoryContext parent,
						const char *name,
						Size minContextSize,
						Size initBlockSize,
						Size maxBlockSize)
{
	Size		firstBlockSize;
	Size		allocSize;
	GenerationContext *set;
	GenerationBlock *block;

	/* Assert we padded GenerationChunk properly */
	StaticAssertStmt(Generation_CHUNKHDRSZ == MAXALIGN(Generation_CHUNKHDRSZ),
					 "sizeof(GenerationChunk) is not maxaligned");
	StaticAssertStmt(offsetof(GenerationChunk, context) + sizeof(MemoryContext) ==
					 Generation_CHUNKHDRSZ,
					 "padding calculation in GenerationChunk is wrong");

	/*
	 * First, validate allocation parameters.  Asserts seem sufficient because
	 * nobody varies their parameters at runtime.  We somewhat arbitrarily
	 * enforce a minimum 1K block size.
	 */
	Assert(initBlockSize == MAXALIGN(initBlockSize) &&
		   initBlockSize >= 1024);
	Assert(maxBlockSize == MAXALIGN(maxBlockSize) &&
		   maxBlockSize >= initBlockSize &&
		   AllocHugeSizeIsValid(maxBlockSize)); /* must be safe to double */
	Assert(minContextSize == 0 ||
		   (minContextSize == MAXALIGN(minContextSize) &&
			minContextSize >= 1024 &&
			minContextSize <= maxBlockSize));

	/* Determine size of initial block */
	allocSize = MAXALIGN(sizeof(GenerationContext)) +
		Generation_BLOCKHDRSZ + Generation_CHUNKHDRSZ;
	if (minContextSize != 0)
		allocSize = Max(allocSize, minContextSize);
	else
		allocSize = Max(allocSize, initBlockSize);

	/*
	 * Allocate the initial block.  Unlike other generation.c blocks, it
	 * starts with the context header and its block header follows that.
	 */
	set = (GenerationContext *) malloc(allocSize);
	if (set == NULL)
	{
		MemoryContextStats(TopMemoryContext);
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory"),
				 errdetail("Failed while creating memory context \"%s\".",
						   name)));
	}

	YbPgMemAddConsumption(allocSize);

	/*
	 * Avoid writing code that can fail between here and MemoryContextCreate;
	 * we'd leak the header if we ereport in this stretch.
	 */
	dlist_init(&set->blocks);

	/* Fill in the initial block's block header */
	block = (GenerationBlock *) (((char *) set) + MAXALIGN(sizeof(GenerationContext)));
	/* determine the block size and initialize it */
	firstBlockSize = allocSize - MAXALIGN(sizeof(GenerationContext));
	GenerationBlockInit(block, firstBlockSize);

	/* add it to the doubly-linked list of blocks */
	dlist_push_head(&set->blocks, &block->node);

	/* use it as the current allocation block */
	set->block = block;

	/* No free block, yet */
	set->freeblock = NULL;

	/* Mark block as not to be released at reset time */
	set->keeper = block;

	/* Fill in GenerationContext-specific header fields */
	set->initBlockSize = initBlockSize;
	set->maxBlockSize = maxBlockSize;
	set->nextBlockSize = initBlockSize;

	/*
	 * Compute the allocation chunk size limit for this context.
	 *
	 * Follows similar ideas as AllocSet, see aset.c for details ...
	 */
	set->allocChunkLimit = maxBlockSize;
	while ((Size) (set->allocChunkLimit + Generation_CHUNKHDRSZ) >
		   (Size) ((Size) (maxBlockSize - Generation_BLOCKHDRSZ) / Generation_CHUNK_FRACTION))
		set->allocChunkLimit >>= 1;

	/* Finally, do the type-independent part of context creation */
	MemoryContextCreate((MemoryContext) set,
						T_GenerationContext,
						&GenerationMethods,
						parent,
						name);

	((MemoryContext) set)->mem_allocated = firstBlockSize;

	return (MemoryContext) set;
}

/*
 * GenerationReset
 *		Frees all memory which is allocated in the given set.
 *
 * The code simply frees all the blocks in the context - we don't keep any
 * keeper blocks or anything like that.
 */
static void
GenerationReset(MemoryContext context)
{
	GenerationContext *set = (GenerationContext *) context;
	dlist_mutable_iter miter;

	AssertArg(GenerationIsValid(set));

#ifdef MEMORY_CONTEXT_CHECKING
	/* Check for corruption and leaks before freeing */
	GenerationCheck(context);
#endif

	/*
	 * NULLify the free block pointer.  We must do this before calling
	 * GenerationBlockFree as that function never expects to free the
	 * freeblock.
	 */
	set->freeblock = NULL;

	dlist_foreach_modify(miter, &set->blocks)
	{
		GenerationBlock *block = dlist_container(GenerationBlock, node, miter.cur);

		if (block == set->keeper)
			GenerationBlockMarkEmpty(block);
		else
			GenerationBlockFree(set, block);
	}

	/* set it so new allocations to make use of the keeper block */
	set->block = set->keeper;

	/* Reset block size allocation sequence, too */
	set->nextBlockSize = set->initBlockSize;

	/* Ensure there is only 1 item in the dlist */
	Assert(!dlist_is_empty(&set->blocks));
	Assert(!dlist_has_next(&set->blocks, dlist_head_node(&set->blocks)));
}

/*
 * GenerationDelete
 *		Free all memory which is allocated in the given context.
 */
static void
GenerationDelete(MemoryContext context)
{
	/* Reset to release all releasable GenerationBlocks */
	GenerationReset(context);
	/* And free the context header and keeper block */
	size_t		freed_sz = context->mem_allocated + Generation_CONTEXTSZ;

	free(context);
	YbPgMemSubConsumption(freed_sz);
}

/*
 * GenerationAlloc
 *		Returns pointer to allocated memory of given size or NULL if
 *		request could not be completed; memory is added to the set.
 *
 * No request may exceed:
 *		MAXALIGN_DOWN(SIZE_MAX) - Generation_BLOCKHDRSZ - Generation_CHUNKHDRSZ
 * All callers use a much-lower limit.
 *
 * Note: when using valgrind, it doesn't matter how the returned allocation
 * is marked, as mcxt.c will set it to UNDEFINED.  In some paths we will
 * return space that is marked NOACCESS - GenerationRealloc has to beware!
 */
static void *
GenerationAlloc(MemoryContext context, Size size)
{
	GenerationContext *set = (GenerationContext *) context;
	GenerationBlock *block;
	GenerationChunk *chunk;
	Size		chunk_size = MAXALIGN(size);
	Size		required_size = chunk_size + Generation_CHUNKHDRSZ;

	/* is it an over-sized chunk? if yes, allocate special block */
	if (chunk_size > set->allocChunkLimit)
	{
		Size		blksize = required_size + Generation_BLOCKHDRSZ;

		block = (GenerationBlock *) malloc(blksize);
		if (block == NULL)
			return NULL;

		YbPgMemAddConsumption(blksize);

		context->mem_allocated += blksize;

		/* block with a single (used) chunk */
		block->blksize = blksize;
		block->nchunks = 1;
		block->nfree = 0;

		/* the block is completely full */
		block->freeptr = block->endptr = ((char *) block) + blksize;

		chunk = (GenerationChunk *) (((char *) block) + Generation_BLOCKHDRSZ);
		chunk->block = block;
		chunk->context = set;
		chunk->size = chunk_size;

#ifdef MEMORY_CONTEXT_CHECKING
		chunk->requested_size = size;
		/* set mark to catch clobber of "unused" space */
		if (size < chunk_size)
			set_sentinel(GenerationChunkGetPointer(chunk), size);
#endif
#ifdef RANDOMIZE_ALLOCATED_MEMORY
		/* fill the allocated space with junk */
		randomize_mem((char *) GenerationChunkGetPointer(chunk), size);
#endif

		/* add the block to the list of allocated blocks */
		dlist_push_head(&set->blocks, &block->node);

		/* Ensure any padding bytes are marked NOACCESS. */
		VALGRIND_MAKE_MEM_NOACCESS((char *) GenerationChunkGetPointer(chunk) + size,
								   chunk_size - size);

		/* Disallow external access to private part of chunk header. */
		VALGRIND_MAKE_MEM_NOACCESS(chunk, GENERATIONCHUNK_PRIVATE_LEN);

		return GenerationChunkGetPointer(chunk);
	}

	/*
	 * Not an oversized chunk.  We try to first make use of the current block,
	 * but if there's not enough space in it, instead of allocating a new
	 * block, we look to see if the freeblock is empty and has enough space.
	 * If not, we'll also try the same using the keeper block.  The keeper
	 * block may have become empty and we have no other way to reuse it again
	 * if we don't try to use it explicitly here.
	 *
	 * We don't want to start filling the freeblock before the current block
	 * is full, otherwise we may cause fragmentation in FIFO type workloads.
	 * We only switch to using the freeblock or keeper block if those blocks
	 * are completely empty.  If we didn't do that we could end up fragmenting
	 * consecutive allocations over multiple blocks which would be a problem
	 * that would compound over time.
	 */
	block = set->block;

	if (block == NULL ||
		GenerationBlockFreeBytes(block) < required_size)
	{
		Size		blksize;
		GenerationBlock *freeblock = set->freeblock;

		if (freeblock != NULL &&
			GenerationBlockIsEmpty(freeblock) &&
			GenerationBlockFreeBytes(freeblock) >= required_size)
		{
			block = freeblock;

			/*
			 * Zero out the freeblock as we'll set this to the current block
			 * below
			 */
			set->freeblock = NULL;
		}
		else if (GenerationBlockIsEmpty(set->keeper) &&
				 GenerationBlockFreeBytes(set->keeper) >= required_size)
		{
			block = set->keeper;
		}
		else
		{
			/*
			 * The first such block has size initBlockSize, and we double the
			 * space in each succeeding block, but not more than maxBlockSize.
			 */
			blksize = set->nextBlockSize;
			set->nextBlockSize <<= 1;
			if (set->nextBlockSize > set->maxBlockSize)
				set->nextBlockSize = set->maxBlockSize;

			/* we'll need a block hdr too, so add that to the required size */
			required_size += Generation_BLOCKHDRSZ;

			/* round the size up to the next power of 2 */
			if (blksize < required_size)
				blksize = pg_nextpower2_size_t(required_size);

			block = (GenerationBlock *) malloc(blksize);

			if (block == NULL)
				return NULL;

			YbPgMemAddConsumption(blksize);

			context->mem_allocated += blksize;

			/* initialize the new block */
			GenerationBlockInit(block, blksize);

			/* add it to the doubly-linked list of blocks */
			dlist_push_head(&set->blocks, &block->node);

			/* Zero out the freeblock in case it's become full */
			set->freeblock = NULL;
		}

		/* and also use it as the current allocation block */
		set->block = block;
	}

	/* we're supposed to have a block with enough free space now */
	Assert(block != NULL);
	Assert((block->endptr - block->freeptr) >= Generation_CHUNKHDRSZ + chunk_size);

	chunk = (GenerationChunk *) block->freeptr;

	/* Prepare to initialize the chunk header. */
	VALGRIND_MAKE_MEM_UNDEFINED(chunk, Generation_CHUNKHDRSZ);

	block->nchunks += 1;
	block->freeptr += (Generation_CHUNKHDRSZ + chunk_size);

	Assert(block->freeptr <= block->endptr);

	chunk->block = block;
	chunk->context = set;
	chunk->size = chunk_size;

#ifdef MEMORY_CONTEXT_CHECKING
	chunk->requested_size = size;
	/* set mark to catch clobber of "unused" space */
	if (size < chunk->size)
		set_sentinel(GenerationChunkGetPointer(chunk), size);
#endif
#ifdef RANDOMIZE_ALLOCATED_MEMORY
	/* fill the allocated space with junk */
	randomize_mem((char *) GenerationChunkGetPointer(chunk), size);
#endif

	/* Ensure any padding bytes are marked NOACCESS. */
	VALGRIND_MAKE_MEM_NOACCESS((char *) GenerationChunkGetPointer(chunk) + size,
							   chunk_size - size);

	/* Disallow external access to private part of chunk header. */
	VALGRIND_MAKE_MEM_NOACCESS(chunk, GENERATIONCHUNK_PRIVATE_LEN);

	return GenerationChunkGetPointer(chunk);
}

/*
 * GenerationBlockInit
 *		Initializes 'block' assuming 'blksize'.  Does not update the context's
 *		mem_allocated field.
 */
static inline void
GenerationBlockInit(GenerationBlock *block, Size blksize)
{
	block->blksize = blksize;
	block->nchunks = 0;
	block->nfree = 0;

	block->freeptr = ((char *) block) + Generation_BLOCKHDRSZ;
	block->endptr = ((char *) block) + blksize;

	/* Mark unallocated space NOACCESS. */
	VALGRIND_MAKE_MEM_NOACCESS(block->freeptr,
							   blksize - Generation_BLOCKHDRSZ);
}

/*
 * GenerationBlockIsEmpty
 *		Returns true iif 'block' contains no chunks
 */
static inline bool
GenerationBlockIsEmpty(GenerationBlock *block)
{
	return (block->nchunks == 0);
}

/*
 * GenerationBlockMarkEmpty
 *		Set a block as empty.  Does not free the block.
 */
static inline void
GenerationBlockMarkEmpty(GenerationBlock *block)
{
#if defined(USE_VALGRIND) || defined(CLOBBER_FREED_MEMORY)
	char	   *datastart = ((char *) block) + Generation_BLOCKHDRSZ;
#endif

#ifdef CLOBBER_FREED_MEMORY
	wipe_mem(datastart, block->freeptr - datastart);
#else
	/* wipe_mem() would have done this */
	VALGRIND_MAKE_MEM_NOACCESS(datastart, block->freeptr - datastart);
#endif

	/* Reset the block, but don't return it to malloc */
	block->nchunks = 0;
	block->nfree = 0;
	block->freeptr = ((char *) block) + Generation_BLOCKHDRSZ;
}

/*
 * GenerationBlockFreeBytes
 *		Returns the number of bytes free in 'block'
 */
static inline Size
GenerationBlockFreeBytes(GenerationBlock *block)
{
	return (block->endptr - block->freeptr);
}

/*
 * GenerationBlockFree
 *		Remove 'block' from 'set' and release the memory consumed by it.
 */
static inline void
GenerationBlockFree(GenerationContext *set, GenerationBlock *block)
{
	/* Make sure nobody tries to free the keeper block */
	Assert(block != set->keeper);
	/* We shouldn't be freeing the freeblock either */
	Assert(block != set->freeblock);

	/* release the block from the list of blocks */
	dlist_delete(&block->node);

	((MemoryContext) set)->mem_allocated -= block->blksize;
	size_t		freed_sz = block->blksize;

#ifdef CLOBBER_FREED_MEMORY
	wipe_mem(block, block->blksize);
#endif

	free(block);
	YbPgMemSubConsumption(freed_sz);
}

/*
 * GenerationFree
 *		Update number of chunks in the block, and if all chunks in the block
 *		are now free then discard the block.
 */
static void
GenerationFree(MemoryContext context, void *pointer)
{
	GenerationContext *set = (GenerationContext *) context;
	GenerationChunk *chunk = GenerationPointerGetChunk(pointer);
	GenerationBlock *block;

	/* Allow access to private part of chunk header. */
	VALGRIND_MAKE_MEM_DEFINED(chunk, GENERATIONCHUNK_PRIVATE_LEN);

	block = chunk->block;

#ifdef MEMORY_CONTEXT_CHECKING
	/* Test for someone scribbling on unused space in chunk */
	if (chunk->requested_size < chunk->size)
		if (!sentinel_ok(pointer, chunk->requested_size))
			elog(WARNING, "detected write past chunk end in %s %p",
				 ((MemoryContext) set)->name, chunk);
#endif

#ifdef CLOBBER_FREED_MEMORY
	wipe_mem(pointer, chunk->size);
#endif

	/* Reset context to NULL in freed chunks */
	chunk->context = NULL;

#ifdef MEMORY_CONTEXT_CHECKING
	/* Reset requested_size to 0 in freed chunks */
	chunk->requested_size = 0;
#endif

	block->nfree += 1;

	Assert(block->nchunks > 0);
	Assert(block->nfree <= block->nchunks);

	/* If there are still allocated chunks in the block, we're done. */
	if (block->nfree < block->nchunks)
		return;

	/* Don't try to free the keeper block, just mark it empty */
	if (block == set->keeper)
	{
		GenerationBlockMarkEmpty(block);
		return;
	}

	/*
	 * If there is no freeblock set or if this is the freeblock then instead
	 * of freeing this memory, we keep it around so that new allocations have
	 * the option of recycling it.
	 */
	if (set->freeblock == NULL || set->freeblock == block)
	{
		/* XXX should we only recycle maxBlockSize sized blocks? */
		set->freeblock = block;
		GenerationBlockMarkEmpty(block);
		return;
	}

	/* Also make sure the block is not marked as the current block. */
	if (set->block == block)
		set->block = NULL;

	/*
	 * The block is empty, so let's get rid of it. First remove it from the
	 * list of blocks, then return it to malloc().
	 */
	dlist_delete(&block->node);

	size_t		freed_sz = block->blksize;

	context->mem_allocated -= block->blksize;
	free(block);
	YbPgMemSubConsumption(freed_sz);
}

/*
 * GenerationRealloc
 *		When handling repalloc, we simply allocate a new chunk, copy the data
 *		and discard the old one. The only exception is when the new size fits
 *		into the old chunk - in that case we just update chunk header.
 */
static void *
GenerationRealloc(MemoryContext context, void *pointer, Size size)
{
	GenerationContext *set = (GenerationContext *) context;
	GenerationChunk *chunk = GenerationPointerGetChunk(pointer);
	GenerationPointer newPointer;
	Size		oldsize;

	/* Allow access to private part of chunk header. */
	VALGRIND_MAKE_MEM_DEFINED(chunk, GENERATIONCHUNK_PRIVATE_LEN);

	oldsize = chunk->size;

#ifdef MEMORY_CONTEXT_CHECKING
	/* Test for someone scribbling on unused space in chunk */
	if (chunk->requested_size < oldsize)
		if (!sentinel_ok(pointer, chunk->requested_size))
			elog(WARNING, "detected write past chunk end in %s %p",
				 ((MemoryContext) set)->name, chunk);
#endif

	/*
	 * Maybe the allocated area already is >= the new size.  (In particular,
	 * we always fall out here if the requested size is a decrease.)
	 *
	 * This memory context does not use power-of-2 chunk sizing and instead
	 * carves the chunks to be as small as possible, so most repalloc() calls
	 * will end up in the palloc/memcpy/pfree branch.
	 *
	 * XXX Perhaps we should annotate this condition with unlikely()?
	 */
	if (oldsize >= size)
	{
#ifdef MEMORY_CONTEXT_CHECKING
		Size		oldrequest = chunk->requested_size;

#ifdef RANDOMIZE_ALLOCATED_MEMORY
		/* We can only fill the extra space if we know the prior request */
		if (size > oldrequest)
			randomize_mem((char *) pointer + oldrequest,
						  size - oldrequest);
#endif

		chunk->requested_size = size;

		/*
		 * If this is an increase, mark any newly-available part UNDEFINED.
		 * Otherwise, mark the obsolete part NOACCESS.
		 */
		if (size > oldrequest)
			VALGRIND_MAKE_MEM_UNDEFINED((char *) pointer + oldrequest,
										size - oldrequest);
		else
			VALGRIND_MAKE_MEM_NOACCESS((char *) pointer + size,
									   oldsize - size);

		/* set mark to catch clobber of "unused" space */
		if (size < oldsize)
			set_sentinel(pointer, size);
#else							/* !MEMORY_CONTEXT_CHECKING */

		/*
		 * We don't have the information to determine whether we're growing
		 * the old request or shrinking it, so we conservatively mark the
		 * entire new allocation DEFINED.
		 */
		VALGRIND_MAKE_MEM_NOACCESS(pointer, oldsize);
		VALGRIND_MAKE_MEM_DEFINED(pointer, size);
#endif

		/* Disallow external access to private part of chunk header. */
		VALGRIND_MAKE_MEM_NOACCESS(chunk, GENERATIONCHUNK_PRIVATE_LEN);

		return pointer;
	}

	/* allocate new chunk */
	newPointer = GenerationAlloc((MemoryContext) set, size);

	/* leave immediately if request was not completed */
	if (newPointer == NULL)
	{
		/* Disallow external access to private part of chunk header. */
		VALGRIND_MAKE_MEM_NOACCESS(chunk, GENERATIONCHUNK_PRIVATE_LEN);
		return NULL;
	}

	/*
	 * GenerationAlloc() may have returned a region that is still NOACCESS.
	 * Change it to UNDEFINED for the moment; memcpy() will then transfer
	 * definedness from the old allocation to the new.  If we know the old
	 * allocation, copy just that much.  Otherwise, make the entire old chunk
	 * defined to avoid errors as we copy the currently-NOACCESS trailing
	 * bytes.
	 */
	VALGRIND_MAKE_MEM_UNDEFINED(newPointer, size);
#ifdef MEMORY_CONTEXT_CHECKING
	oldsize = chunk->requested_size;
#else
	VALGRIND_MAKE_MEM_DEFINED(pointer, oldsize);
#endif

	/* transfer existing data (certain to fit) */
	memcpy(newPointer, pointer, oldsize);

	/* free old chunk */
	GenerationFree((MemoryContext) set, pointer);

	return newPointer;
}

/*
 * GenerationGetChunkSpace
 *		Given a currently-allocated chunk, determine the total space
 *		it occupies (including all memory-allocation overhead).
 */
static Size
GenerationGetChunkSpace(MemoryContext context, void *pointer)
{
	GenerationChunk *chunk = GenerationPointerGetChunk(pointer);
	Size		result;

	VALGRIND_MAKE_MEM_DEFINED(chunk, GENERATIONCHUNK_PRIVATE_LEN);
	result = chunk->size + Generation_CHUNKHDRSZ;
	VALGRIND_MAKE_MEM_NOACCESS(chunk, GENERATIONCHUNK_PRIVATE_LEN);
	return result;
}

/*
 * GenerationIsEmpty
 *		Is a GenerationContext empty of any allocated space?
 */
static bool
GenerationIsEmpty(MemoryContext context)
{
	GenerationContext *set = (GenerationContext *) context;
	dlist_iter	iter;

	dlist_foreach(iter, &set->blocks)
	{
		GenerationBlock *block = dlist_container(GenerationBlock, node, iter.cur);

		if (block->nchunks > 0)
			return false;
	}

	return true;
}

/*
 * GenerationStats
 *		Compute stats about memory consumption of a Generation context.
 *
 * printfunc: if not NULL, pass a human-readable stats string to this.
 * passthru: pass this pointer through to printfunc.
 * totals: if not NULL, add stats about this context into *totals.
 * print_to_stderr: print stats to stderr if true, elog otherwise.
 *
 * XXX freespace only accounts for empty space at the end of the block, not
 * space of freed chunks (which is unknown).
 */
static void
GenerationStats(MemoryContext context,
				MemoryStatsPrintFunc printfunc, void *passthru,
				MemoryContextCounters *totals, bool print_to_stderr)
{
	GenerationContext *set = (GenerationContext *) context;
	Size		nblocks = 0;
	Size		nchunks = 0;
	Size		nfreechunks = 0;
	Size		totalspace;
	Size		freespace = 0;
	dlist_iter	iter;

	/* Include context header in totalspace */
	totalspace = MAXALIGN(sizeof(GenerationContext));

	dlist_foreach(iter, &set->blocks)
	{
		GenerationBlock *block = dlist_container(GenerationBlock, node, iter.cur);

		nblocks++;
		nchunks += block->nchunks;
		nfreechunks += block->nfree;
		totalspace += block->blksize;
		freespace += (block->endptr - block->freeptr);
	}

	if (printfunc)
	{
		char		stats_string[200];

		snprintf(stats_string, sizeof(stats_string),
				 "%zu total in %zu blocks (%zu chunks); %zu free (%zu chunks); %zu used",
				 totalspace, nblocks, nchunks, freespace,
				 nfreechunks, totalspace - freespace);
		printfunc(context, passthru, stats_string, print_to_stderr);
	}

	if (totals)
	{
		totals->nblocks += nblocks;
		totals->freechunks += nfreechunks;
		totals->totalspace += totalspace;
		totals->freespace += freespace;
	}
}


#ifdef MEMORY_CONTEXT_CHECKING

/*
 * GenerationCheck
 *		Walk through chunks and check consistency of memory.
 *
 * NOTE: report errors as WARNING, *not* ERROR or FATAL.  Otherwise you'll
 * find yourself in an infinite loop when trouble occurs, because this
 * routine will be entered again when elog cleanup tries to release memory!
 */
static void
GenerationCheck(MemoryContext context)
{
	GenerationContext *gen = (GenerationContext *) context;
	const char *name = context->name;
	dlist_iter	iter;
	Size		total_allocated = 0;

	/* walk all blocks in this context */
	dlist_foreach(iter, &gen->blocks)
	{
		GenerationBlock *block = dlist_container(GenerationBlock, node, iter.cur);
		int			nfree,
					nchunks;
		char	   *ptr;

		total_allocated += block->blksize;

		/*
		 * nfree > nchunks is surely wrong.  Equality is allowed as the block
		 * might completely empty if it's the freeblock.
		 */
		if (block->nfree > block->nchunks)
			elog(WARNING, "problem in Generation %s: number of free chunks %d in block %p exceeds %d allocated",
				 name, block->nfree, block, block->nchunks);

		/* Now walk through the chunks and count them. */
		nfree = 0;
		nchunks = 0;
		ptr = ((char *) block) + Generation_BLOCKHDRSZ;

		while (ptr < block->freeptr)
		{
			GenerationChunk *chunk = (GenerationChunk *) ptr;

			/* Allow access to private part of chunk header. */
			VALGRIND_MAKE_MEM_DEFINED(chunk, GENERATIONCHUNK_PRIVATE_LEN);

			/* move to the next chunk */
			ptr += (chunk->size + Generation_CHUNKHDRSZ);

			nchunks += 1;

			/* chunks have both block and context pointers, so check both */
			if (chunk->block != block)
				elog(WARNING, "problem in Generation %s: bogus block link in block %p, chunk %p",
					 name, block, chunk);

			/*
			 * Check for valid context pointer.  Note this is an incomplete
			 * test, since palloc(0) produces an allocated chunk with
			 * requested_size == 0.
			 */
			if ((chunk->requested_size > 0 && chunk->context != gen) ||
				(chunk->context != gen && chunk->context != NULL))
				elog(WARNING, "problem in Generation %s: bogus context link in block %p, chunk %p",
					 name, block, chunk);

			/* now make sure the chunk size is correct */
			if (chunk->size < chunk->requested_size ||
				chunk->size != MAXALIGN(chunk->size))
				elog(WARNING, "problem in Generation %s: bogus chunk size in block %p, chunk %p",
					 name, block, chunk);

			/* is chunk allocated? */
			if (chunk->context != NULL)
			{
				/* check sentinel, but only in allocated blocks */
				if (chunk->requested_size < chunk->size &&
					!sentinel_ok(chunk, Generation_CHUNKHDRSZ + chunk->requested_size))
					elog(WARNING, "problem in Generation %s: detected write past chunk end in block %p, chunk %p",
						 name, block, chunk);
			}
			else
				nfree += 1;

			/*
			 * If chunk is allocated, disallow external access to private part
			 * of chunk header.
			 */
			if (chunk->context != NULL)
				VALGRIND_MAKE_MEM_NOACCESS(chunk, GENERATIONCHUNK_PRIVATE_LEN);
		}

		/*
		 * Make sure we got the expected number of allocated and free chunks
		 * (as tracked in the block header).
		 */
		if (nchunks != block->nchunks)
			elog(WARNING, "problem in Generation %s: number of allocated chunks %d in block %p does not match header %d",
				 name, nchunks, block, block->nchunks);

		if (nfree != block->nfree)
			elog(WARNING, "problem in Generation %s: number of free chunks %d in block %p does not match header %d",
				 name, nfree, block, block->nfree);
	}

	Assert(total_allocated == context->mem_allocated);
}

#endif							/* MEMORY_CONTEXT_CHECKING */
