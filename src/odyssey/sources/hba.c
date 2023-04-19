
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <odyssey.h>

void od_hba_init(od_hba_t *hba)
{
	pthread_mutex_init(&hba->lock, NULL);
	od_hba_rules_init(&hba->rules);
}

void od_hba_free(od_hba_t *hba)
{
	od_hba_rules_free(&hba->rules);
	pthread_mutex_destroy(&hba->lock);
}

void od_hba_lock(od_hba_t *hba)
{
	pthread_mutex_lock(&hba->lock);
}

void od_hba_unlock(od_hba_t *hba)
{
	pthread_mutex_unlock(&hba->lock);
}

void od_hba_reload(od_hba_t *hba, od_hba_rules_t *rules)
{
	od_hba_lock(hba);

	od_list_init(&hba->rules);
	memcpy(&hba->rules, &rules, sizeof(hba->rules));

	od_hba_unlock(hba);
}

bool od_hba_validate_addr(od_hba_rule_t *rule, struct sockaddr_storage *sa)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in *rule_addr = (struct sockaddr_in *)&rule->addr;
	struct sockaddr_in *rule_mask = (struct sockaddr_in *)&rule->mask;
	in_addr_t client_addr = sin->sin_addr.s_addr;
	in_addr_t client_net = rule_mask->sin_addr.s_addr & client_addr;
	return (client_net ^ rule_addr->sin_addr.s_addr) == 0;
}

bool od_hba_validate_addr6(od_hba_rule_t *rule, struct sockaddr_storage *sa)
{
	struct sockaddr_in6 *sin = (struct sockaddr_in6 *)sa;
	struct sockaddr_in6 *rule_addr = (struct sockaddr_in6 *)&rule->addr;
	struct sockaddr_in6 *rule_mask = (struct sockaddr_in6 *)&rule->mask;
	for (int i = 0; i < 16; ++i) {
		uint8_t client_net_byte = rule_mask->sin6_addr.s6_addr[i] &
					  sin->sin6_addr.s6_addr[i];
		if (client_net_byte ^ rule_addr->sin6_addr.s6_addr[i]) {
			return false;
		}
	}

	return true;
}

bool od_hba_validate_name(char *client_name, od_hba_rule_name_t *name,
			  char *client_other_name)
{
	if (name->flags & OD_HBA_NAME_ALL) {
		return true;
	}

	if ((name->flags & OD_HBA_NAME_SAMEUSER) &&
	    strcmp(client_name, client_other_name) == 0) {
		return true;
	}

	od_list_t *i;
	od_hba_rule_name_item_t *item;
	od_list_foreach(&name->values, i)
	{
		item = od_container_of(i, od_hba_rule_name_item_t, link);
		if (item->value != NULL &&
		    strcmp(client_name, item->value) == 0) {
			return true;
		}
	}

	return false;
}

int od_hba_process(od_client_t *client)
{
	od_instance_t *instance = client->global->instance;
	od_hba_t *hba = client->global->hba;
	od_list_t *i;
	od_hba_rule_t *rule;
	od_hba_rules_t *rules;

	if (instance->config.hba_file == NULL) {
		return OK_RESPONSE;
	}

	struct sockaddr_storage sa;
	int salen = sizeof(sa);
	struct sockaddr *saddr = (struct sockaddr *)&sa;
	int rc = machine_getpeername(client->io.io, saddr, &salen);
	if (rc == -1)
		return -1;

	od_hba_lock(hba);
	rules = &hba->rules;
	od_hba_unlock(hba);

	od_list_foreach(rules, i)
	{
		rule = od_container_of(i, od_hba_rule_t, link);
		if (sa.ss_family == AF_UNIX) {
			if (rule->connection_type != OD_CONFIG_HBA_LOCAL)
				continue;
		} else if (rule->connection_type == OD_CONFIG_HBA_LOCAL) {
			continue;
		} else if (rule->connection_type == OD_CONFIG_HBA_HOSTSSL &&
			   !client->startup.is_ssl_request) {
			continue;
		} else if (rule->connection_type == OD_CONFIG_HBA_HOSTNOSSL &&
			   client->startup.is_ssl_request) {
			continue;
		} else if (sa.ss_family == AF_INET) {
			if (rule->addr.ss_family != AF_INET ||
			    !od_hba_validate_addr(rule, &sa)) {
				continue;
			}
		} else if (sa.ss_family == AF_INET6) {
			if (rule->addr.ss_family != AF_INET6 ||
			    !od_hba_validate_addr6(rule, &sa)) {
				continue;
			}
		}

		if (!od_hba_validate_name(client->rule->db_name,
					  &rule->database,
					  client->rule->user_name)) {
			continue;
		}
		if (!od_hba_validate_name(client->rule->user_name, &rule->user,
					  client->rule->db_name)) {
			continue;
		}

		rc = rule->auth_method == OD_CONFIG_HBA_ALLOW ? OK_RESPONSE :
								NOT_OK_RESPONSE;

		return rc;
	}

	return NOT_OK_RESPONSE;
}
