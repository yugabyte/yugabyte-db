#ifndef ODYSSEY_HBA_RULE_H
#define ODYSSEY_HBA_RULE_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define OD_HBA_NAME_ALL 1
#define OD_HBA_NAME_SAMEUSER 2

typedef struct od_hba_rule od_hba_rule_t;

typedef enum {
	OD_CONFIG_HBA_LOCAL,
	OD_CONFIG_HBA_HOST,
	OD_CONFIG_HBA_HOSTSSL,
	OD_CONFIG_HBA_HOSTNOSSL
} od_hba_rule_conn_type_t;

typedef enum {
	OD_CONFIG_HBA_ALLOW,
	OD_CONFIG_HBA_DENY,
} od_hba_rule_auth_method_t;

typedef struct od_hba_rule_name_item od_hba_rule_name_item_t;

struct od_hba_rule_name_item {
	char *value;
	od_list_t link;
};

typedef struct od_hba_rule_name od_hba_rule_name_t;

struct od_hba_rule_name {
	unsigned int flags;
	od_list_t values;
};

struct od_hba_rule {
	od_hba_rule_conn_type_t connection_type;
	od_hba_rule_name_t database;
	od_hba_rule_name_t user;
	struct sockaddr_storage addr;
	struct sockaddr_storage mask;
	od_hba_rule_auth_method_t auth_method;
	od_list_t link;
};

typedef od_list_t od_hba_rules_t;

od_hba_rule_name_item_t *od_hba_rule_name_item_add(od_hba_rule_name_t *name);
od_hba_rule_t *od_hba_rule_create();
void od_hba_rule_free(od_hba_rule_t *hba);
void od_hba_rules_init(od_hba_rules_t *rules);
void od_hba_rules_free(od_hba_rules_t *rules);
void od_hba_rules_add(od_hba_rules_t *rules, od_hba_rule_t *rule);
#endif /* ODYSSEY_HBA_RULE_H */
