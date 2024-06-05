
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

int mm_thread_create(mm_thread_t *thread, int stack_size,
		     mm_thread_function_t function, void *arg)
{
	pthread_attr_t attr;
	int rc;
	rc = pthread_attr_init(&attr);
	if (rc != 0)
		return -1;
	rc = pthread_attr_setstacksize(&attr, stack_size);
	if (rc != 0) {
		pthread_attr_destroy(&attr);
		return -1;
	}
	thread->function = function;
	thread->arg = arg;
	rc = pthread_create(&thread->id, &attr, function, arg);
	pthread_attr_destroy(&attr);
	if (rc != 0)
		return -1;
	return 0;
}

int mm_thread_join(mm_thread_t *thread)
{
	int rc;
	rc = pthread_join(thread->id, NULL);
	return rc;
}

int mm_thread_set_name(mm_thread_t *thread, char *name)
{
	int rc;
	rc = pthread_setname_np(thread->id, name);
	return rc;
}

int mm_thread_disable_cancel(void)
{
	int unused;
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &unused);
	return pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &unused);
}
