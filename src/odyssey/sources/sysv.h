#ifndef ODYSSEY_SYSV_H
#define ODYSSEY_SYSV_H

static inline size_t od_get_ncpu()
{
#ifdef _SC_NPROCESSORS_ONLN
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif
	return 1;
}

#endif /* ODYSSEY_SYSV_H */
