/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/heap_utils.h
 *
 * Utility to maintain min/max heap.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <common/hashfn.h>
#include <utils/hsearch.h>
#include "commands/commands_common.h"

#ifndef HEAP_UTILS_H
#define HEAP_UTILS_H

/**
 * Defines how the heap is sorted.
 * For minheap, the comparator should return true if first < second.
 * For maxheap, the comparator should return true if first > second.
 */
typedef bool (*HeapComparator)(const void *first, const void *second);

/*
 * binaryheap
 *		heapNodes		variable-length array of "space" nodes
 *		bh_size			how many nodes are currently in "nodes"
 *		bh_space		how many nodes can be stored in "nodes"
 *		heapComparator	comparison function to define the heap property
 */
typedef struct BinaryHeap
{
	bson_value_t *heapNodes;
	int64_t heapSize;
	int64_t heapSpace;
	HeapComparator heapComparator;
} BinaryHeap;

BinaryHeap * AllocateHeap(int64_t capacity, HeapComparator comparator);
void PushToHeap(BinaryHeap *heap, const bson_value_t *value);
bson_value_t PopFromHeap(BinaryHeap *heap);
bson_value_t TopHeap(BinaryHeap *heap);
void FreeHeap(BinaryHeap *heap);

#endif
