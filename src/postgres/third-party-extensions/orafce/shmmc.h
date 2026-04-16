#ifndef __SHMMC__
#define __SHMMC__

extern void  ora_sinit(void *ptr, size_t size, bool create);
extern void* ora_salloc(size_t size);
extern void* ora_srealloc(void *ptr, size_t size);
extern void  ora_sfree(void* ptr);
extern char* ora_sstrcpy(char *str);
extern char* ora_scstring(text *str);
extern void* salloc(size_t size);
extern void* srealloc(void *ptr,size_t size);
#endif
