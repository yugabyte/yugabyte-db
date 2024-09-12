/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/heap_utils.c
 *
 * Utility to maintain min/max heap.
 *
 *-------------------------------------------------------------------------
 */

#include "utils/heap_utils.h"

#include "commands/commands_common.h"

static void Swap(bson_value_t *a, bson_value_t *b);
static void Heapify(bson_value_t *array, int64_t itemsInArray, int64_t index,
					HeapComparator comparator);

/*
 *
 * Returns a pointer to a newly-allocated heap that has the capacity to
 * store the given number of nodes, with the heap property defined by
 * the given comparator function
 */
BinaryHeap *
AllocateHeap(int64_t capacity, HeapComparator comparator)
{
	/* Allocate the heap nodes */
	BinaryHeap *heap = palloc(sizeof(BinaryHeap));

	heap->heapNodes = (bson_value_t *) palloc(sizeof(bson_value_t) * capacity);
	heap->heapSize = 0;
	heap->heapSpace = capacity;
	heap->heapComparator = comparator;

	return heap;
}


/*
 * Pushes the value to the heap.
 */
void
PushToHeap(BinaryHeap *heap, const bson_value_t *value)
{
	Assert(heap->heapSize < heap->heapSpace);

	int64_t index = heap->heapSize++;
	heap->heapNodes[index] = *value;

	/* Ensures that the heap property is maintained after insertion. */
	while (index != 0 && !heap->heapComparator(&heap->heapNodes[(index - 1) / 2],
											   &heap->heapNodes[index]))
	{
		/* If the parent node does not satisfy the heap property with the current node.*/
		Swap(&heap->heapNodes[(index - 1) / 2], &heap->heapNodes[index]);

		/* Move up the tree by setting the current index to its parent's index. */
		index = (index - 1) / 2;
	}
}


/*
 * Pops the top of the heap.
 */
bson_value_t
PopFromHeap(BinaryHeap *heap)
{
	Assert(heap->heapSize > 0);

	bson_value_t result = heap->heapNodes[0];

	if (heap->heapSize == 1)
	{
		heap->heapSize--;
		return result;
	}

	heap->heapNodes[0] = heap->heapNodes[--heap->heapSize];
	Heapify(heap->heapNodes, heap->heapSize, 0, heap->heapComparator);

	return result;
}


/*
 * Returns the top of the heap.
 */
bson_value_t
TopHeap(BinaryHeap *heap)
{
	Assert(heap->heapSize > 0);

	return heap->heapNodes[0];
}


/*
 * Releases memory used by the given binaryheap.
 */
void
FreeHeap(BinaryHeap *heap)
{
	Assert(heap->heapNodes != NULL && heap != NULL);
	pfree(heap->heapNodes);
	pfree(heap);
}


/*
 * Swaps the two values
 */
static void
Swap(bson_value_t *a, bson_value_t *b)
{
	bson_value_t temp = *a;
	*a = *b;
	*b = temp;
}


/*
 * Recursively heapifies the array
 */
static void
Heapify(bson_value_t *array, int64_t itemsInArray, int64_t index,
		HeapComparator comparator)
{
	int64_t limit = index;
	int64_t leftIndex = 2 * index + 1;
	int64_t rightIndex = 2 * index + 2;

	if (leftIndex < itemsInArray && comparator(&array[leftIndex], &array[limit]))
	{
		limit = leftIndex;
	}

	if (rightIndex < itemsInArray && comparator(&array[rightIndex], &array[limit]))
	{
		limit = rightIndex;
	}

	if (limit != index)
	{
		Swap(&array[index], &array[limit]);
		Heapify(array, itemsInArray, limit, comparator);
	}
}
