/*
 *
 * Shared memory control - based on alocating chunks aligned on
 * asize array (fibonachi), and dividing free bigger block.
 *
 */

#include "postgres.h"
#include "shmmc.h"
#include "orafce.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if PG_VERSION_NUM >= 160000

#include "varatt.h"

#endif

#define LIST_ITEMS  512

int context;

typedef struct {
	size_t size;
	void* first_byte_ptr;
	bool dispossible;
/*	int16 context; */
} list_item;

typedef struct {
	int list_c;
	size_t max_size;
	vardata data[1];	 /* flexible array member */
} mem_desc;

#define MAX_SIZE 82688

static size_t asize[] = {
	32,
	64,       96,   160,  256,
	416,     672,  1088,  1760,
	2848,   4608,  7456, 12064,
	19520, 31584, 51104, 82688};


int *list_c = NULL;
list_item *list = NULL;
size_t max_size;

int cycle = 0;


/* align requested size */

static int
ptr_comp(const void* a, const void* b)
{
	ptrdiff_t d;

	list_item *_a = (list_item*) a;
	list_item *_b = (list_item*) b;

	d = (uintptr_t)_a->first_byte_ptr - (uintptr_t)_b->first_byte_ptr;

	return d > 0 ? 1 : (d < 0 ? -1 : 0);
}

char *
ora_sstrcpy(char *str)
{
	size_t len;
	char *result;

	len = strlen(str);
	if (NULL != (result = ora_salloc(len+1)))
		memcpy(result, str, len + 1);
	else
		ereport(ERROR,
			(errcode(ERRCODE_OUT_OF_MEMORY),
			errmsg("out of memory"),
			errdetail("Failed while allocation block %d bytes in shared memory.", (int) len+1),
			errhint("Increase SHMEMMSGSZ and recompile package.")));

	return result;
}

char *
ora_scstring(text *str)
{
	int len;
	char *result;

	len = VARSIZE_ANY_EXHDR(str);

	if (NULL != (result = ora_salloc(len+1)))
	{
		memcpy(result, VARDATA_ANY(str), len);
		result[len] = '\0';
	}
	else
		ereport(ERROR,
			(errcode(ERRCODE_OUT_OF_MEMORY),
			errmsg("out of memory"),
			errdetail("Failed while allocation block %d bytes in shared memory.", (int) len+1),
			errhint("Increase SHMEMMSGSZ and recompile package.")));

	return result;
}

/*
 * Compact the list of slots, by merging adjacent unused slots into larger
 * slots.
 */
static void
defragmentation()
{
	int src, target;

	/* Sort the array to pointer order */
	qsort(list, *list_c, sizeof(list_item), ptr_comp);

	/* Merge adjacent dispossible slots, and move up other slots */
	target = 0;
	for (src = 0; src < *list_c; src++)
	{
		if (target > 0 &&
			list[src].dispossible &&
			list[target - 1].dispossible)
		{
			list[target - 1].size += list[src].size;
		}
		else
		{
			if (src != target)
				memcpy(&list[target], &list[src], sizeof(list_item));
			target++;
		}
	}
	*list_c = target;
}

static size_t
align_size(size_t size)
{
	int i;

	/* default, we can allocate max MAX_SIZE memory block */

	for (i = 0; i < 17; i++)
		if (asize[i] >= size)
			return asize[i];

	ereport(ERROR,
		   (errcode(ERRCODE_OUT_OF_MEMORY),
		    errmsg("too much large memory block request"),
		    errdetail("Failed while allocation block %lu bytes in shared memory.", (unsigned long) size),
		    errhint("Increase MAX_SIZE constant, fill table a_size and recompile package.")));

	return 0;
}

/*
  initialize shared memory. It works in two modes, create and no create.
  No create is used for mounting shared memory buffer. Top of memory is
  used for list_item array.
*/

void
ora_sinit(void *ptr, size_t size, bool create)
{
	if (list == NULL)
	{
		mem_desc *m = (mem_desc*)ptr;
		list = (list_item*)m->data;
		list_c = &m->list_c;
		max_size = m->max_size = size;

		if (create)
		{
			list[0].size = size - sizeof(list_item)*LIST_ITEMS - sizeof(mem_desc);
			list[0].first_byte_ptr = ((char *) &m->data) + sizeof(list_item)*LIST_ITEMS;
			list[0].dispossible = true;
			*list_c = 1;
		}
	}
}


void*
ora_salloc(size_t size)
{
	size_t aligned_size;
	int repeat_c;
	void *ptr = NULL;

	aligned_size = align_size(size);

	for (repeat_c = 0; repeat_c < 2; repeat_c++)
	{
		size_t	max_min = max_size;
		int		select = -1;
		int		i;

		/* find first good free block */
		for (i = 0; i < *list_c; i++)
		{
			if (list[i].dispossible)
			{
				/* If this block is just the right size, return it */
				if (list[i].size == aligned_size)
				{
					list[i].dispossible = false;
					ptr = list[i].first_byte_ptr;
					/* list[i].context = context; */

					return ptr;
				}

				if (list[i].size > aligned_size && list[i].size < max_min)
				{
					max_min = list[i].size;
					select = i;
				}
			}
		}

		/* If no suitable free slot found, defragment and try again. */
		if (select == -1 || *list_c == LIST_ITEMS)
		{
			defragmentation();
			continue;
		}

		/*
		 * A slot larger than required was found. Divide it to avoid wasting
		 * space, and return the slot of the right size.
		 */
		list[*list_c].size = list[select].size - aligned_size;
		list[*list_c].first_byte_ptr = (char*)list[select].first_byte_ptr + aligned_size;
		list[*list_c].dispossible = true;
		list[select].size = aligned_size;
		list[select].dispossible = false;
		/* list[select].context = context; */
		ptr = list[select].first_byte_ptr;
		*list_c += 1;
		break;
	}

	return ptr;
}

void
ora_sfree(void *ptr)
{
	int			i;

/*
	if (cycle++ % 100 == 0)
	{
		size_t suma = 0;
		for (i = 0; i < *list_c; i++)
			if (list[i].dispossible)
				suma += list[i].size;
		elog(NOTICE, "=============== FREE MEM REPORT === %10d ================", suma);
	}
*/

	for (i = 0; i < *list_c; i++)
	{
		if (list[i].first_byte_ptr == ptr)
		{
			list[i].dispossible = true;
			/* list[i].context = -1; */
			memset(ptr, '#', list[i].size);
			return;
		}
	}

	ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("corrupted pointer"),
			 errdetail("Failed while reallocating memory block in shared memory."),
			 errhint("Please report this bug to the package authors.")));
}


void*
ora_srealloc(void *ptr, size_t size)
{
	void *result;
	size_t aux_s = 0;
	int i;

	for (i = 0; i < *list_c; i++)
		if (list[i].first_byte_ptr == ptr)
		{
			if (align_size(size) <= list[i].size)
				return ptr;
			aux_s = list[i].size;
		}

	if (aux_s == 0)
		ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("corrupted pointer"),
			errdetail("Failed while reallocating memory block in shared memory."),
			errhint("Please report this bug to the package authors.")));


	if (NULL != (result = ora_salloc(size)))
	{
		memcpy(result, ptr, aux_s);
		ora_sfree(ptr);
	}

	return result;
}

/*
 *  alloc shared memory, raise exception if not
 */

void*
salloc(size_t size)
{
	void* result;

	if (NULL == (result = ora_salloc(size)))
		ereport(ERROR,
			(errcode(ERRCODE_OUT_OF_MEMORY),
			errmsg("out of memory"),
			errdetail("Failed while allocation block %lu bytes in shared memory.", (unsigned long) size),
			errhint("Increase SHMEMMSGSZ and recompile package.")));

	return result;
}

void*
srealloc(void *ptr, size_t size)
{
	void* result;

	if (NULL == (result = ora_srealloc(ptr, size)))
		ereport(ERROR,
			(errcode(ERRCODE_OUT_OF_MEMORY),
			errmsg("out of memory"),
			errdetail("Failed while reallocation block %lu bytes in shared memory.", (unsigned long) size),
			errhint("Increase SHMEMMSGSZ and recompile package.")));

	return result;
}
