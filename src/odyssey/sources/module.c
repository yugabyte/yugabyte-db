/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <machinarium.h>
#include <odyssey.h>

void od_modules_init(od_module_t *module)
{
	od_list_init(&module->link);
}

od_module_t *od_modules_find(od_module_t *modules, char *target_module_path)
{
	od_list_t *i;
	od_list_foreach(&modules->link, i)
	{
		od_module_t *m;
		m = od_container_of(i, od_module_t, link);
		if (strcmp(m->path, target_module_path) == 0) {
			return m;
		}
	}

	return NULL;
}

int od_target_module_add(od_logger_t *logger, od_module_t *modules,
			 char *target_module_path)
{
	od_list_t *i;
	od_list_foreach(&modules->link, i)
	{
		od_module_t *m;
		m = od_container_of(i, od_module_t, link);
		if (strcmp(m->path, target_module_path) == 0) {
			goto module_exists;
		}
	}

	void *handle;
	od_module_t *module_ptr;
	char *err;

	handle = od_dlopen(target_module_path);
	if (!handle) {
		od_log(logger, "load_module", NULL, NULL,
		       "errors while openning %s module: %s",
		       target_module_path, dlerror());
		goto error;
	}
	module_ptr = od_load_module(handle);
	if ((err = dlerror()) != NULL) {
		goto error_close_handle;
	}

	if (strlen(module_ptr->path) + strlen(target_module_path) + 1 >
	    sizeof(module_ptr->path))
		goto error_close_handle;

	module_ptr->handle = handle;
	od_list_init(&module_ptr->link);
	od_list_append(&modules->link, &module_ptr->link);
	strcat(module_ptr->path, target_module_path);

	if (module_ptr->module_init_cb) {
		return module_ptr->module_init_cb(logger);
	}

	return OD_MODULE_CB_OK_RETCODE;

module_exists:
	if (logger == NULL) {
		/* most probably its logger is not ready yet */
	} else {
		od_log(logger, "od_load_module", NULL, NULL,
		       "od_load_module: skip load module %s: was already loaded!",
		       target_module_path);
	}
	return OD_MODULE_CB_FAIL_RETCODE;

error_close_handle:
	od_dlclose(handle);
error:
	err = od_dlerror();
	if (logger) {
		od_log(logger, "od_load_module", NULL, NULL,
		       "od_load_module: failed to load module %s", err);
	}
	return OD_MODULE_CB_FAIL_RETCODE;
}

static inline void od_module_free(od_module_t *module)
{
	od_list_unlink(&module->link);
}

int od_target_module_unload(od_logger_t *logger, od_module_t *modules,
			    char *target_module)
{
	char *err;
	od_list_t *i;
	od_list_foreach(&modules->link, i)
	{
		od_module_t *m;
		m = od_container_of(i, od_module_t, link);
		if (strcmp(m->path, target_module) == 0) {
			int rc;
			rc = m->unload_cb();
			if (rc != OD_MODULE_CB_OK_RETCODE)
				return -1;
			void *h = m->handle;
			m->handle = NULL;
			od_module_free(m);
			// because we cannot access handle after calling dlclose
			if (od_dlclose(h)) {
				goto error;
			}

			return OD_MODULE_CB_OK_RETCODE;
		}
	}

	od_log(logger, "od target module unload failed", NULL, NULL,
	       "od_module_unload: failed to find specified module to unload %s",
	       target_module);

	return OD_MODULE_CB_FAIL_RETCODE;
error:
	err = od_dlerror();
	od_log(logger, "od unload module error", NULL, NULL,
	       "od_module_unload: %s", err);

	return OD_MODULE_CB_FAIL_RETCODE;
}

int od_modules_unload(od_logger_t *logger, od_module_t *modules)
{
	char *err;
	od_list_t *i, *n;
	od_list_foreach_safe(&modules->link, i, n)
	{
		od_module_t *m;
		m = od_container_of(i, od_module_t, link);
		int rc;
		rc = m->unload_cb();
		if (rc != OD_MODULE_CB_OK_RETCODE)
			return -1;
		void *h = m->handle;
		m->handle = NULL;
		od_module_free(m);
		// because we cannot access handle after calling dlclose
		if (od_dlclose(h)) {
			goto error;
		}
	}

	return OD_MODULE_CB_OK_RETCODE;
error:
	err = od_dlerror();
	od_log(logger, "od unload module error", NULL, NULL,
	       "od_module_unload: %s", err);

	return OD_MODULE_CB_FAIL_RETCODE;
}

int od_modules_unload_fast(od_module_t *modules)
{
	od_list_t *i;
	od_list_foreach(&modules->link, i)
	{
		od_module_t *m;
		m = od_container_of(i, od_module_t, link);
		void *h = m->handle;
		m->handle = NULL;
		od_module_free(m);
		// because we cannot access handle after calling dlclose
		if (od_dlclose(h)) {
			return OD_MODULE_CB_FAIL_RETCODE;
		}
	}
	return OD_MODULE_CB_OK_RETCODE;
}
