#ifndef KIWI_PARAM_LOCK_H
#define KIWI_PARAM_LOCK_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_params_lock kiwi_params_lock_t;

struct kiwi_params_lock {
	pthread_mutex_t lock;
	kiwi_params_t params;
};

static inline void kiwi_params_lock_init(kiwi_params_lock_t *pl)
{
	pthread_mutex_init(&pl->lock, NULL);
	kiwi_params_init(&pl->params);
}

static inline void kiwi_params_lock_free(kiwi_params_lock_t *pl)
{
	pthread_mutex_destroy(&pl->lock);
	kiwi_params_free(&pl->params);
}

static inline int kiwi_params_lock_count(kiwi_params_lock_t *pl)
{
	pthread_mutex_lock(&pl->lock);
	int rc = pl->params.count;
	pthread_mutex_unlock(&pl->lock);
	return rc;
}

static inline int kiwi_params_lock_copy(kiwi_params_lock_t *pl,
					kiwi_params_t *dest)
{
	pthread_mutex_lock(&pl->lock);
	int rc;
	rc = kiwi_params_copy(dest, &pl->params);
	pthread_mutex_unlock(&pl->lock);
	return rc;
}

static inline int kiwi_params_lock_set_once(kiwi_params_lock_t *pl,
					    kiwi_params_t *params)
{
	pthread_mutex_lock(&pl->lock);
	if (pl->params.count > 0) {
		pthread_mutex_unlock(&pl->lock);
		return 0;
	}
	pl->params = *params;
	pthread_mutex_unlock(&pl->lock);
	return 1;
}

#endif /* KIWI_PARAM_LOCK_H */
