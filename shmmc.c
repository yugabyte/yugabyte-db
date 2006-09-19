/*
 *
 * Shared memory control - based on alocating chunks alligned on
 * asize array (fibonachi), and dividing free bigger block.
 *
 */
 
#include "postgres.h"
#include "shmmc.h"
#include "stdlib.h"
#include "string.h"


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
	int max_size;
	char *data;
} mem_desc;

#define MAX_SIZE 82688

static size_t asize[] = {
	32,
	64,       96,   160,  256, 
    416,     672,  1088,  1760, 
    2848,   4608,  7456, 12064, 
    19520, 31584, 51104, 82688};


int *list_c = 0;
list_item *list = NULL;
size_t max_size;

int cycle = 0;


/* allign requested size */

static int 
ptr_comp(const void* a, const void* b)
{
	list_item *_a = (list_item*) a;
	list_item *_b = (list_item*) b;

	return (long)_a->first_byte_ptr - (long)_b->first_byte_ptr;
}

#define MOVE_CUR 1
#define ADD_CUR  2

char *
ora_sstrcpy(char *str)
{
	int len;
	char *result;

	len = strlen(str);
	if (NULL != (result = ora_salloc(len+1)))
		memcpy(result, str, len + 1);
	else
		ereport(ERROR,                                                                                 
                	(errcode(ERRCODE_OUT_OF_MEMORY),                                               
                	errmsg("out of memory"),                                                      
                	errdetail("Failed while allocation block %d bytes in shared memory.", len+1),
                	errhint("Increase SHMEMMSGSZ and recompile package.")));
	
	return result;
}

char *
ora_scstring(text *str)
{
	int len;
	char *result;

	len = VARSIZE(str) - VARHDRSZ;

	if (NULL != (result = ora_salloc(len+1)))
	{
		memcpy(result, VARDATA(str), len);
		result[len] = '\0';
	}
	else
		ereport(ERROR,                                                                                 
                	(errcode(ERRCODE_OUT_OF_MEMORY),                                               
                	errmsg("out of memory"),                                                      
                	errdetail("Failed while allocation block %d bytes in shared memory.", len+1),
                	errhint("Increase SHMEMMSGSZ and recompile package.")));

	return result;
}

static void
defragmentation()
{
	int i, w;
	int state = MOVE_CUR;

	qsort(list, *list_c, sizeof(list_item), ptr_comp);
	
	/* list strip -  every field have to check or move */

	w = 0;
	for (i = 0; i < *list_c; i++)
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
			{
				list[w].size += list[i].size;
			}
			else
			{
				if (i != ++w)
					memcpy(&list[w], &list[i], sizeof(list_item));
				w += 1;
				state = MOVE_CUR;
			}
		}
	}
	*list_c = w + 1 + (state == ADD_CUR ? 1:0);

}

static size_t
allign_size(size_t size)
{
	int i;

	/* default, we can allocate max MAX_SIZE memory block */

	for(i = 0; i < 17; i++)
		if (asize[i] >= size)
			return asize[i];

	ereport(ERROR,                                                                                 
                   (errcode(ERRCODE_OUT_OF_MEMORY),                                               
                    errmsg("too much large memory block request"),                                                      
                    errdetail("Failed while allocation block %d bytes in shared memory.", size),
                    errhint("Increase MAX_SIZE constant, fill table a_size and recompile package.")));                     

	return 0;
}

/* 
  inicialize shared memory. It works in two modes, create and no create.
  No create is used for mounting shared memory buffer. Top of memory is
  used for list_item array. 
*/

void 
ora_sinit(void *ptr, size_t size, bool create)
{
	if (list == NULL)
	{
		mem_desc *m = (mem_desc*)ptr;
		list = (list_item*)&m->data;
		list_c = &m->list_c;
		max_size = m->max_size = size;

		if (create)
		{
			list[0].size = size - sizeof(list_item)*LIST_ITEMS - sizeof(mem_desc);
			list[0].first_byte_ptr = &m->data + sizeof(list_item)*LIST_ITEMS;
			list[0].dispossible = true;
			*list_c = 1;			
		}
	}
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
		for(i = 0; i < *list_c; i++)
			if (list[i].dispossible)
			{
				if (list[i].size == alligned_size)
				{
					list[i].dispossible = false;
					ptr = list[i].first_byte_ptr;
					/* list[i].context = context; */

					return ptr;
				}
				
				if (list[i].size > alligned_size && list[i].size < max_min)
				{
					max_min = list[i].size;
					select = i;
				}
			}
		
		/* if I haven't well block or free slot */

		if (select == -1 || *list_c == LIST_ITEMS)
		{
			defragmentation();
			continue;
		}
		
		/* I have to divide block */
		
		list[*list_c].size = list[select].size - alligned_size;
		list[*list_c].first_byte_ptr = (char*)list[select].first_byte_ptr + alligned_size;
		list[*list_c].dispossible = true;
		list[select].size = alligned_size;
		list[select].dispossible = false;
		/* list[select].context = context; */
		ptr = list[select].first_byte_ptr;
		*list_c += 1;
 		break;
	}

	return ptr;
}

void 
ora_sfree(void* ptr)
{
	int i;

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
		if (list[i].first_byte_ptr == ptr)
		{
			list[i].dispossible = true;
			/* list[i].context = -1; */
			memset(list[i].first_byte_ptr, '#', list[i].size);
			return;
		}
	
	ereport(ERROR,                                                                                 
            	(errcode(ERRCODE_INTERNAL_ERROR),
            	errmsg("corrupted pointer"),
            	errdetail("Failed while reallocating memory block in shared memory."),
            	errhint("Report this bug to autors.")));


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
			if (allign_size(size) <= list[i].size)
				return ptr;
			aux_s = list[i].size;
		}

	if (aux_s == 0)
		ereport(ERROR,                                                                                 
                	(errcode(ERRCODE_INTERNAL_ERROR),
                	errmsg("corrupted pointer"),
                	errdetail("Failed while reallocating memory block in shared memory."),
                	errhint("Report this bug to autors.")));

		
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
                	errdetail("Failed while allocation block %d bytes in shared memory.", size),
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
                	errdetail("Failed while reallocation block %d bytes in shared memory.", size),
                	errhint("Increase SHMEMMSGSZ and recompile package.")));

        return result;
}
