
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

void mm_machinemgr_init(mm_machinemgr_t *mgr)
{
	pthread_spin_init(&mgr->lock, PTHREAD_PROCESS_PRIVATE);
	mm_list_init(&mgr->list);
	mgr->count = 0;
	mgr->seq = 0;
}

void mm_machinemgr_free(mm_machinemgr_t *mgr)
{
	pthread_spin_destroy(&mgr->lock);
}

int mm_machinemgr_count(mm_machinemgr_t *mgr)
{
	int count;
	pthread_spin_lock(&mgr->lock);
	count = mgr->count;
	pthread_spin_unlock(&mgr->lock);
	return count;
}

void mm_machinemgr_add(mm_machinemgr_t *mgr, mm_machine_t *machine)
{
	pthread_spin_lock(&mgr->lock);
	machine->id = mgr->seq++;
	mm_list_append(&mgr->list, &machine->link);
	mgr->count++;
	pthread_spin_unlock(&mgr->lock);
}

void mm_machinemgr_delete(mm_machinemgr_t *mgr, mm_machine_t *machine)
{
	pthread_spin_lock(&mgr->lock);
	mm_list_unlink(&machine->link);
	mgr->count--;
	pthread_spin_unlock(&mgr->lock);
}

mm_machine_t *mm_machinemgr_delete_by_id(mm_machinemgr_t *mgr, uint64_t id)
{
	pthread_spin_lock(&mgr->lock);
	mm_list_t *i;
	mm_list_foreach(&mgr->list, i)
	{
		mm_machine_t *machine;
		machine = mm_container_of(i, mm_machine_t, link);
		if (machine->id == id) {
			mm_list_unlink(&machine->link);
			mgr->count--;
			pthread_spin_unlock(&mgr->lock);
			return machine;
		}
	}
	pthread_spin_unlock(&mgr->lock);
	return NULL;
}

mm_machine_t *mm_machinemgr_find_by_id(mm_machinemgr_t *mgr, uint64_t id)
{
	pthread_spin_lock(&mgr->lock);
	mm_list_t *i;
	mm_list_foreach(&mgr->list, i)
	{
		mm_machine_t *machine;
		machine = mm_container_of(i, mm_machine_t, link);
		if (machine->id == id) {
			pthread_spin_unlock(&mgr->lock);
			return machine;
		}
	}
	pthread_spin_unlock(&mgr->lock);
	return NULL;
}
