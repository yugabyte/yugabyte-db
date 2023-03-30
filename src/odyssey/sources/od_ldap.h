#ifndef ODYSSEY_LDAP_H
#define ODYSSEY_LDAP_H

#define LDAP_DEPRECATED 1
#include <ldap.h>

typedef struct {
	od_id_t id;

	LDAP *conn;
	// connect url
	od_ldap_endpoint_t *endpoint; // link to actual settings;
	od_server_state_t state;

	od_global_t *global;
	void *route;
	int idle_timestamp;

	od_list_t link;
} od_ldap_server_t;

extern od_retcode_t od_auth_ldap(od_client_t *cl, kiwi_password_t *tok);

extern od_retcode_t od_ldap_server_free(od_ldap_server_t *serv);
extern od_ldap_server_t *od_ldap_server_allocate();
extern od_retcode_t od_ldap_server_init(od_logger_t *logger,
					od_ldap_server_t *serv,
					od_rule_t *rule);
extern od_retcode_t od_ldap_server_prepare(od_logger_t *logger,
					   od_ldap_server_t *serv,
					   od_rule_t *rule,
					   od_client_t *client);
extern od_ldap_server_t *od_ldap_server_pull(od_logger_t *logger,
					     od_rule_t *rule, bool auth_pool);
#endif /* ODYSSEY_LDAP_H */
