#ifndef KIWI_PARAM_H
#define KIWI_PARAM_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_param kiwi_param_t;
typedef struct kiwi_params kiwi_params_t;

struct kiwi_param {
	int name_len;
	int value_len;
	kiwi_param_t *next;
	char data[];
};

struct kiwi_params {
	int count;
	kiwi_param_t *list;
};

static inline kiwi_param_t *kiwi_param_allocate(char *name, int name_len,
						char *value, int value_len)
{
	kiwi_param_t *param;
	param = malloc(sizeof(kiwi_param_t) + name_len + value_len);
	if (param == NULL)
		return NULL;
	param->name_len = name_len;
	param->value_len = value_len;
	param->next = NULL;
	memcpy(param->data, name, name_len);
	memcpy(param->data + name_len, value, value_len);
	return param;
}

static inline void kiwi_param_free(kiwi_param_t *param)
{
	free(param);
}

static inline char *kiwi_param_name(kiwi_param_t *param)
{
	return param->data;
}

static inline char *kiwi_param_value(kiwi_param_t *param)
{
	return param->data + param->name_len;
}

static inline int kiwi_param_compare_name(kiwi_param_t *param, char *name,
					  int name_len)
{
	int rc;
	rc = param->name_len == name_len &&
	     strncasecmp(kiwi_param_name(param), name, name_len) == 0;
	return rc;
}

static inline int kiwi_param_compare(kiwi_param_t *a, kiwi_param_t *b)
{
	return kiwi_param_compare_name(a, kiwi_param_name(b), b->name_len);
}

static inline void kiwi_params_init(kiwi_params_t *params)
{
	params->list = NULL;
	params->count = 0;
}

static inline void kiwi_params_free(kiwi_params_t *params)
{
	kiwi_param_t *param = params->list;
	while (param) {
		kiwi_param_t *next = param->next;
		kiwi_param_free(param);
		param = next;
	}
}

static inline void kiwi_params_add(kiwi_params_t *params, kiwi_param_t *param)
{
	param->next = params->list;
	params->list = param;
	params->count++;
}

static inline int kiwi_params_copy(kiwi_params_t *dest, kiwi_params_t *src)
{
	kiwi_param_t *param = src->list;
	while (param) {
		kiwi_param_t *new_param;
		new_param = kiwi_param_allocate(kiwi_param_name(param),
						param->name_len,
						kiwi_param_value(param),
						param->value_len);
		if (new_param == NULL)
			return -1;
		kiwi_params_add(dest, new_param);
		param = param->next;
	}
	return 0;
}

static inline void kiwi_params_replace(kiwi_params_t *params,
				       kiwi_param_t *new_param)
{
	kiwi_param_t *param = params->list;
	kiwi_param_t *prev = NULL;
	while (param) {
		if (kiwi_param_compare(param, new_param)) {
			new_param->next = param->next;
			if (prev)
				prev->next = new_param;
			else
				params->list = new_param;
			kiwi_param_free(param);
			return;
		}
		prev = param;
		param = param->next;
	}
	kiwi_params_add(params, new_param);
}

static inline kiwi_param_t *kiwi_params_find(kiwi_params_t *params, char *name,
					     int name_len)
{
	kiwi_param_t *param = params->list;
	while (param) {
		if (kiwi_param_compare_name(param, name, name_len))
			return param;
		param = param->next;
	}
	return NULL;
}

#endif /* KIWI_PARAM_H */
