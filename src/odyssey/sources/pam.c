
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <machinarium.h>
#include <odyssey.h>

#include <security/pam_appl.h>

struct sss {
	char *psswd;
	char *res;
};

static int od_pam_conversation(int msgc, const struct pam_message **msgv,
			       struct pam_response **rspv, void *authdata)
{
	od_pam_auth_data_t *auth_data = authdata;
	if (msgc < 1 || msgv == NULL)
		return PAM_CONV_ERR;

	*rspv = malloc(msgc * sizeof(struct pam_response));
	if (*rspv == NULL)
		return PAM_CONV_ERR;
	memset(*rspv, 0, msgc * sizeof(struct pam_response));

	int rc = PAM_SUCCESS;
	int counter = 0;
	for (; counter < msgc; counter++) {
		od_list_t *i;
		od_list_foreach(&auth_data->link, i)
		{
			od_pam_auth_data_t *param;
			param = od_container_of(i, od_pam_auth_data_t, link);
			if (param->msg_style == msgv[counter]->msg_style) {
				(*rspv)[counter].resp = strdup(param->value);
				break;
			}
		}
		if ((*rspv)[counter].resp == NULL) {
			rc = PAM_CONV_ERR;
			break;
		}
	}

	if (rc != PAM_SUCCESS) {
		for (; counter >= 0; counter--) {
			od_list_t *i;
			od_list_foreach(&auth_data->link, i)
			{
				od_pam_auth_data_t *param;
				param = od_container_of(i, od_pam_auth_data_t,
							link);
				if (param->msg_style ==
				    msgv[counter]->msg_style) {
					free((*rspv)[counter].resp);
					break;
				}
			}
		}
		free(*rspv);
		*rspv = NULL;
	}

	return rc;
}

int od_pam_auth(char *od_pam_service, char *usrname,
		od_pam_auth_data_t *auth_data, machine_io_t *io)
{
	struct pam_conv conv = {
		od_pam_conversation,
		.appdata_ptr = auth_data,
	};

	pam_handle_t *pamh = NULL;
	int rc;
	rc = pam_start(od_pam_service, usrname, &conv, &pamh);
	if (rc != PAM_SUCCESS)
		goto error;

	char peer[128];
	od_getpeername(io, peer, sizeof(peer), 1, 0);
	rc = pam_set_item(pamh, PAM_RHOST, peer);
	if (rc != PAM_SUCCESS) {
		goto error;
	}

	rc = pam_authenticate(pamh, PAM_SILENT);
	if (rc != PAM_SUCCESS)
		goto error;

	rc = pam_acct_mgmt(pamh, PAM_SILENT);
	if (rc != PAM_SUCCESS)
		goto error;

	rc = pam_end(pamh, rc);
	if (rc != PAM_SUCCESS)
		return -1;

	return 0;

error:
	pam_end(pamh, rc);
	return -1;
}

void od_pam_convert_passwd(od_pam_auth_data_t *d, char *passwd)
{
	od_list_t *i;
	od_list_foreach(&d->link, i)
	{
		od_pam_auth_data_t *param =
			od_container_of(i, od_pam_auth_data_t, link);
		if (param->msg_style == PAM_PROMPT_ECHO_OFF) {
			param->value = strdup(passwd);
		}
		return;
	}
	od_pam_auth_data_t *passwd_data = malloc(sizeof(od_pam_auth_data_t));
	passwd_data->msg_style = PAM_PROMPT_ECHO_OFF;
	passwd_data->value = strdup(passwd);

	od_list_append(&d->link, &passwd_data->link);
}

od_pam_auth_data_t *od_pam_auth_data_create(void)
{
	od_pam_auth_data_t *d;
	d = (od_pam_auth_data_t *)malloc(sizeof(*d));
	if (d == NULL)
		return NULL;
	od_list_init(&d->link);
	return d;
}

void od_pam_auth_data_free(od_pam_auth_data_t *d)
{
	od_list_t *i;
	od_list_foreach(&d->link, i)
	{
		od_pam_auth_data_t *current =
			od_container_of(i, od_pam_auth_data_t, link);
		free(current->value);
	}
	od_list_unlink(&d->link);
	free(d);
}
