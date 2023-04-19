#ifndef MM_MACHINE_MGR_H
#define MM_MACHINE_MGR_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_machinemgr mm_machinemgr_t;

struct mm_machinemgr {
	pthread_spinlock_t lock;
	mm_list_t list;
	int count;
	uint64_t seq;
};

void mm_machinemgr_init(mm_machinemgr_t *);
void mm_machinemgr_free(mm_machinemgr_t *);
int mm_machinemgr_count(mm_machinemgr_t *);
void mm_machinemgr_add(mm_machinemgr_t *, mm_machine_t *);
void mm_machinemgr_delete(mm_machinemgr_t *, mm_machine_t *);
mm_machine_t *mm_machinemgr_delete_by_id(mm_machinemgr_t *, uint64_t);
mm_machine_t *mm_machinemgr_find_by_id(mm_machinemgr_t *, uint64_t);

#endif /* MM_MACHINE_MGR_H */
