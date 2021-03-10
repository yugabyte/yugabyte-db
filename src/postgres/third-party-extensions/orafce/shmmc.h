#ifndef __SHMMC__
#define __SHMMC__

void  ora_sinit(void *ptr, size_t size, bool create);
void* ora_salloc(size_t size);
void* ora_srealloc(void *ptr, size_t size);
void  ora_sfree(void* ptr);
char* ora_sstrcpy(char *str);
char* ora_scstring(text *str);
void* salloc(size_t size);
void* srealloc(void *ptr,size_t size);
#endif
