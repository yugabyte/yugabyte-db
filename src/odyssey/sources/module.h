#ifndef ODYSSEY_MODULE_H
#define ODYSSEY_MODULE_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define OD_LOAD_MODULE "od_module"
#define od_load_module(handle) (od_module_t *)od_dlsym(handle, OD_LOAD_MODULE)

#define OD_MODULE_CB_OK_RETCODE 0
#define OD_MODULE_CB_FAIL_RETCODE -1

/*  init */
typedef int (*module_init_cb_t)(od_logger_t *logger);

/* auth */
typedef int (*client_auth_attempt_cb_t)(od_client_t *c);
typedef int (*client_auth_complete_cb_t)(od_client_t *c, int rc);
typedef int (*client_disconnect_cb_t)(od_client_t *c, od_frontend_status_t s);

/* config */
typedef int (*config_rule_init_cb_t)(od_rule_t *rule, od_config_reader_t *cr,
				     od_token_t *token);

typedef int (*config_module_init_db_t)(od_config_reader_t *cr);

/* reload */
typedef od_retcode_t (*od_config_reload_cb_t)(od_list_t *added,
					      od_list_t *deleted);

/* nonexcluzive auth cb */
typedef od_retcode_t (*od_auth_cleartext_cb_t)(od_client_t *cl,
					       kiwi_password_t *tok);

/* unload */
typedef int (*module_unload_cb_t)(void);

#define MAX_MODULE_PATH_LEN 2048

struct od_module {
	void *handle;
	char path[MAX_MODULE_PATH_LEN];

	/*             Handlers            */
	/*---------------------------------*/
	module_init_cb_t module_init_cb;

	client_auth_attempt_cb_t auth_attempt_cb;
	client_auth_complete_cb_t auth_complete_cb;
	client_disconnect_cb_t disconnect_cb;

	config_rule_init_cb_t config_rule_init_cb;
	config_module_init_db_t config_module_init_db;

	od_config_reload_cb_t od_config_reload_cb;
	od_auth_cleartext_cb_t od_auth_cleartext_cb;

	module_unload_cb_t unload_cb;

	/*---------------------------------*/
	od_list_t link;
};

typedef struct od_module od_module_t;

extern void od_modules_init(od_module_t *module);

extern int od_target_module_add(od_logger_t *logger, od_module_t *modules,
				char *target_module_path);

extern od_module_t *od_modules_find(od_module_t *modules,
				    char *target_module_path);

extern int od_target_module_unload(od_logger_t *logger, od_module_t *modules,
				   char *target_module);
extern int od_modules_unload(od_logger_t *logger, od_module_t *modules);
// function tio perform "fast" unload all modules,
// here we do not wait for module-defined unload callback
extern int od_modules_unload_fast(od_module_t *modules);

#endif /* ODYSSEY_MODULE_H */
