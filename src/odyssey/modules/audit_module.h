#ifndef ODYSSEY_AUDIT_MODULE_H
#define ODYSSEY_AUDIT_MODULE_H

/*
 * Odyssey module.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include "module.h"

int audit_auth_attempt_cb(od_client_t *c);
int audit_auth_complete_cb(od_client_t *c, bool auth_ok);
int audit_disconnect_cb(od_client_t *c, od_status_t s);
int audit_config_init(od_rule_t *rule, od_config_reader_t *cr,
		      od_token_t *token);
int audit_auth_unload();
int audit_init_cb();

#endif // ODYSSEY_AUDIT_MODULE_H
