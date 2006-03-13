/*
 *
 * Shared memory controll
 *
 */

#include "postgres.h"
#include "shmmc.h"
#include "stdlib.h"

#define LIST_ITEMS  512

typedef struct {
	size_t size;
	void* first_byte_ptr;
	bool dispossible;
} list_item;


#define MAX_SIZE 82688

static size_t asize[] = {
	32,
	64,       96,   160,  256, 
    416,     672,  1088,  1760, 
    2848,   4608,  7456, 12064, 
    19520, 31584, 51104, 82688};

int list_c = 0;
size_t max_size;

list_item *list;

/* allign requested size */

void
show_memory()
{
	int i;
	for (i = 0; i < list_c; i++)
		elog(NOTICE, "%d %d", list[i].size, list[i].dispossible);
}

static int 
ptr_comp(const void* a, const void* b)
{
	list_item *_a = (list_item*) a;
	list_item *_b = (list_item*) b;

	return (long)_a->first_byte_ptr - (long)_b->first_byte_ptr;
}

#define MOVE_CUR 1
#define ADD_CUR  2

void
defragmentation()
{
	int i, w;
	int state = MOVE_CUR;

	qsort(list, list_c, sizeof(list_item), ptr_comp);
	
    /* list strip -  every field have to check or move */

	w = 0;
	for (i = 0; i < list_c; i++)
	{
		if (state == MOVE_CUR)
		{
			if (i != w)
				memcpy(&list[w], &list[i], sizeof(list_item));
			state = (list[i].dispossible) ? ADD_CUR : MOVE_CUR;
			w = w + (state == MOVE_CUR ? 1 : 0);
		} 
		else if (state == ADD_CUR)
		{
			if (list[i].dispossible)
				list[w].size += list[i].size;
			else
			{
				if (i != ++w)
					memcpy(&list[w], &list[i], sizeof(list_item));;
				state = MOVE_CUR;
			}
		}
	}
	list_c = w + 1;(state == ADD_CUR ? 1:0);

}

static size_t
allign_size(size_t size)
{
	int i;

	for(i = 0; i < 17; i++)
		if (asize[i] >= size)
			return asize[i];

	elog(ERROR, "Can't alloc block of size %d bytes.", size);
}

void 
ora_sinit(void *ptr, size_t size)
{
	list = ptr;
	list[0].size = size - sizeof(list_item)*LIST_ITEMS;;
	list[0].first_byte_ptr = (char*)ptr + sizeof(list_item)*LIST_ITEMS;
	list[0].dispossible = true;
	list_c = 1;
	max_size = size;
}

void*
ora_salloc(size_t size)
{
	size_t alligned_size;
	size_t max_min;
	int select;
	int i;
	int repeat_c;
	void *ptr = NULL;

	alligned_size = allign_size(size);

	max_min = max_size;
	select = -1;

	for(repeat_c = 0; repeat_c < 2; repeat_c++)
	{

		/* find first good free block */
		for(i = 0; i < list_c; i++)
			if (list[i].dispossible)
			{
				if (list[i].size == alligned_size)
				{
					list[i].dispossible = false;
					ptr = list[i].first_byte_ptr;
					return ptr;
				}
				
				if (list[i].size > alligned_size && list[i].size < max_min)
				{
					max_min = list[i].size;
					select = i;
				}
			}
		
		/* if I haven't well block or free slot */

		if (select == -1 || list_c == LIST_ITEMS)
		{
			defragmentation();
			continue;
		}
		
		/* I have to divide block */
		
		list[list_c].size = list[select].size - alligned_size;
		list[list_c].first_byte_ptr = (char*)list[select].first_byte_ptr + alligned_size;
		list[list_c].dispossible = true;
		list[select].size = alligned_size;
		list[select].dispossible = false;
		ptr = list[select].first_byte_ptr;
		list_c += 1;
		break;
	}

	return ptr;
}

void 
ora_sfree(void* ptr)
{
	int i;
	for (i = 0; i < list_c; i++)
		if (list[i].first_byte_ptr == ptr)
		{
			list[i].dispossible = true;
			break;
		}
}

	
