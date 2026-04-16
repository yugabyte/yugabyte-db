#ifndef OD_THREAD_GLOBAL_H

#define OD_THREAD_GLOBAL_H

typedef struct {
	od_conn_eject_info *info;
	int wid; /* worker id */
	/* TODO: store here some metainfo about incomming connections flow and use in somehow */
} od_thread_global;

extern od_retcode_t od_thread_global_init(od_thread_global **gl);
extern od_thread_global **od_thread_global_get(void);
extern od_retcode_t od_thread_global_free(od_thread_global *gl);

#endif /*  OD_THREAD_GLOBAL_H */
