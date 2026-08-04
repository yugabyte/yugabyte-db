#ifndef ODYSSEY_AUTH_H
#define ODYSSEY_AUTH_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

int od_auth_frontend(od_client_t *);
int od_auth_backend(od_server_t *, machine_msg_t *, od_client_t *);

typedef enum {
    OD_AUTH_OK = 0,
    OD_AUTH_CLEARTEXT = 3,
    OD_AUTH_MD5 = 5,
    OD_AUTH_SASL = 10,
    OD_AUTH_SASL_CONTINUE = 11,
    OD_AUTH_SASL_FINAL = 12,
} yb_od_authtype_t;

static inline char *yb_authtype_to_string(yb_od_authtype_t type) {
    switch(type) {
        case OD_AUTH_OK:
            return "AuthOk";
        case OD_AUTH_CLEARTEXT:
            return "AuthCleartextPassword";
        case OD_AUTH_MD5:
            return "AuthMD5Password";
        case OD_AUTH_SASL:
            return "AuthSASLInit";
        case OD_AUTH_SASL_CONTINUE:
            return "AuthSASLContinue";
        case OD_AUTH_SASL_FINAL:
            return "AuthSASLFinal";
        default:
            return "Unknown";
    }
}

#endif /* ODYSSEY_AUTH_H */
