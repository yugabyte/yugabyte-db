/*
 * Odyssey module.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include "audit_module.h"

od_module_t od_module = {
	.module_init_cb = audit_init_cb,
	.auth_complete_cb = audit_auth_complete_cb,
	.auth_attempt_cb = audit_auth_attempt_cb,
	.disconnect_cb = audit_disconnect_cb,
	.unload_cb = audit_auth_unload,
	.config_init_cb = audit_config_init,
};

int audit_auth_attempt_cb(od_client_t *c)
{
	return OD_MODULE_CB_OK_RETCODE;
}

int audit_auth_complete_cb(od_client_t *c, bool auth_ok)
{
	FILE *fptr;
	fptr = fopen("/tmp/audit_usr.log", "a");

	if (fptr == NULL) {
		printf("Error!");
		return OD_MODULE_CB_FAIL_RETCODE;
	}
	if (auth_ok) {
		fprintf(fptr, "usr successfully logged in: usrname = %s",
			c->startup.user.value);
	} else {
		fprintf(fptr, "usr failed to log in: usrname = %s",
			c->startup.user.value);
	}

	fclose(fptr);
	return OD_MODULE_CB_OK_RETCODE;
}

int audit_disconnect_cb(od_client_t *c, od_status_t s)
{
	return OD_MODULE_CB_OK_RETCODE;
}

int audit_init_cb()
{
	return OD_MODULE_CB_OK_RETCODE;
}

int audit_auth_unload(void)
{
	return OD_MODULE_CB_OK_RETCODE;
}

int audit_config_init(od_rule_t *rule, od_config_reader_t *cr,
		      od_token_t *token)
{
	return OD_MODULE_CB_OK_RETCODE;
}
