---
title: Troubleshoot LDAP issues
headerTitle:
linkTitle: LDAP issues
description: Troubleshoot LDAP issues
menu:
  stable_yugabyte-platform:
    identifier: ldap-issues
    parent: troubleshoot-yp
    weight: 40
type: docs
---

## Troubleshooting LDAP

Laboratory machines sometimes lack an appropriate intermediate certificate in order to trust the LDAP server certificate. You can prepend the environment variable `LDAPTLS_REQCERT=never` to test connectivity with ldapsearch:

```sh
LDAPTLS_REQCERT=never ldapsearch -x -H ldaps://ldapserver.example.org -b dc=example,dc=org 'uid=adam' -D "cn=admin,dc=example,dc=org" -w adminpassword
```

There are two cases where explicit intermediate CA configuration is needed:

* ldapsearch works correctly with `LDAPTLS_REQCERT=never` but fails otherwise.
* ldapsearch works correctly, but database authentication still fails with a PostgreSQL error message such as "LDAP diagnostics: error:1416F086:SSL routines:tls_process_server_certificate:certificate verify failed".

In either case, you need to define the intermediate CA in `$HOME/ldaprc` or `$HOME/.ldaprc` for the `yugabyte` user. The following example file `/home/yugabyte/ldaprc` shows the `TLS_CACERT` option pointing to the CA certificate used by the LDAP server. You need to obtain this CA file and place it locally on each client machine.

```output
TLS_CACERT /etc/ssl/certs/ca-bundle.trust.crt
```

If the `TLS_CACERT` option is not set in `$HOME/ldaprc`, it will not work in the system-wide OpenLDAP configuration file `/etc/openldap/ldap.conf`.
