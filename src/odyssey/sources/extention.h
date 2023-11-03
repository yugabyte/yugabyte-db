#ifndef ODYSSEY_ADDON_H
#define ODYSSEY_ADDON_H

typedef struct od_extention od_extention_t;

struct od_extention {
	od_module_t *modules;

	od_global_t *glob;
};

static inline od_retcode_t od_extentions_init(od_extention_t *extentions)
{
	extentions->modules = malloc(sizeof(od_module_t));
	od_modules_init(extentions->modules);

	return OK_RESPONSE;
}

static inline od_retcode_t od_extention_free(od_logger_t *l,
					     od_extention_t *extentions)
{
	if (extentions->modules) {
		od_modules_unload(l, extentions->modules);
	}

	free(extentions->modules);
	extentions->modules = NULL;

	return OK_RESPONSE;
}

#endif /* ODYSSEY_ADDON_H */
