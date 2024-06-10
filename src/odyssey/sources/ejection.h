#ifndef OD_EJECTION_H
#define OD_EJECTION_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

/* only for online restart */
typedef struct {
	pthread_mutex_t mu;
	int last_conn_drop_ts;
} od_conn_eject_info;

extern od_retcode_t od_conn_eject_info_init(od_conn_eject_info **dst);
extern od_retcode_t od_conn_eject_info_free(od_conn_eject_info *ptr);

#endif /* OD_EJECTION_H */
