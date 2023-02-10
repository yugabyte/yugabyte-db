
#ifndef OD_ODYSSEY_LDAP_ENDPOINT_H
#define OD_ODYSSEY_LDAP_ENDPOINT_H

typedef struct {
	pthread_mutex_t lock;
	char *name;

	char *ldapserver;
	uint64_t ldapport;

	// either null, ldap or ldaps
	char *ldapscheme;

	char *ldapprefix;
	char *ldapsuffix;
	char *ldapbindpasswd;
	char *ldapsearchfilter;
	char *ldapsearchattribute;
	char *ldapscope;

	char *ldapbasedn;
	char *ldapbinddn;
	// preparsed connect url
	char *ldapurl;

	void *ldap_search_pool;
	void *ldap_auth_pool;

	machine_channel_t *wait_bus;

	od_list_t link;
} od_ldap_endpoint_t;

/* ldap endpoints ADD/REMOVE API */

extern od_retcode_t od_ldap_endpoint_prepare(od_ldap_endpoint_t *);

extern od_retcode_t od_ldap_endpoint_add(od_ldap_endpoint_t *ldaps,
					 od_ldap_endpoint_t *target);

extern od_ldap_endpoint_t *od_ldap_endpoint_find(od_list_t *ldaps,
						 char *target);

extern od_retcode_t od_ldap_endpoint_remove(od_ldap_endpoint_t *ldaps,
					    od_ldap_endpoint_t *target);
// -------------------------------------------------------
extern od_ldap_endpoint_t *od_ldap_endpoint_alloc();
extern od_retcode_t od_ldap_endpoint_init(od_ldap_endpoint_t *);
extern od_retcode_t od_ldap_endpoint_free(od_ldap_endpoint_t *le);
/* ldap_storage_credentials */

typedef struct {
	char *name;
	char *lsc_username;
	char *lsc_password;

	od_list_t link;
} od_ldap_storage_credentials_t;

static inline void od_ldap_endpoint_lock(od_ldap_endpoint_t *le)
{
	pthread_mutex_lock(&le->lock);
}

static inline void od_ldap_endpoint_unlock(od_ldap_endpoint_t *le)
{
	pthread_mutex_unlock(&le->lock);
}

static inline int od_ldap_endpoint_wait(od_ldap_endpoint_t *le,
					uint32_t time_ms)
{
	machine_msg_t *msg;
	msg = machine_channel_read(le->wait_bus, time_ms);
	if (msg) {
		machine_msg_free(msg);
		return 0;
	}
	return -1;
}

static inline int od_ldap_endpoint_signal(od_ldap_endpoint_t *le)
{
	machine_msg_t *msg;
	msg = machine_msg_create(0);
	if (msg == NULL) {
		return -1;
	}
	machine_channel_write(le->wait_bus, msg);
	return 0;
}

extern od_ldap_storage_credentials_t *
od_ldap_storage_credentials_find(od_list_t *storage_users, char *target);

// -------------------------------------------------------
extern od_ldap_storage_credentials_t *od_ldap_storage_credentials_alloc();
extern od_retcode_t
od_ldap_storage_credentials_free(od_ldap_storage_credentials_t *lsc);
#endif /* OD_ODYSSEY_LDAP_ENDPOINT_H */
