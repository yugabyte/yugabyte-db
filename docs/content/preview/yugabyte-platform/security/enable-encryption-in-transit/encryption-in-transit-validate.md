---
title: Validate certificates
headerTitle: Validate certificates
linkTitle: Validate certificates
description: Validate certificates.
menu:
  preview_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: eit-validate
    weight: 50
type: docs
---

When configuring and using certificates, SSL issues may occasionally arise. You can validate your certificates and keys as follows:

1. Verify that the CA CRT and CA private key match by executing the following commands:

   ```shell
   openssl rsa -noout -modulus -in ca.key | openssl md5
   openssl x509 -noout -modulus -in ca.crt | openssl md5

   \# outputs should match
   ```

2. Verify that the CA CRT is actually a certificate authority by executing the following command:

   ```shell
   openssl x509 -text -noout -in ca.crt

   \# Look for fields

   X509v3 Basic Constraints:

     CA:TRUE
   ```

3. Verify that certificates and keys are in PEM format (as opposed to the DER or other format). If these artifacts are not in the PEM format and you require assistance with converting them or identifying the format, consult [Converting certificates](https://support.globalsign.com/ssl/ssl-certificates-installation/converting-certificates-openssl).

4. Ensure that the private key does not have a passphrase associated with it. For information on how to identify this condition, see [Decrypt an encrypted SSL RSA private key](https://techjourney.net/how-to-decrypt-an-enrypted-ssl-rsa-private-key-pem-key/).

## Enforcing TLS versions

As TLS 1.0 and 1.1 are no longer accepted by PCI compliance, and considering significant vulnerabilities around these versions of the protocol, it is recommended that you migrate to TLS 1.2 or later versions.

You can set the TLS version for node-to-node and client-node communication. To enforce TLS 1.2, add the following flag for YB-TServer:

```shell
ssl_protocols = tls12
```

To enforce the minimum TLS version of 1.2, you need to specify all available subsequent versions for YB-TServer, as follows:

```shell
ssl_protocols = tls12,tls13
```

In addition, as the `ssl_protocols` setting does not propagate to PostgreSQL, it is recommended that you specify the minimum TLS version (`ssl_min_protocol_version`) for PostgreSQL by setting the following YB-TServer gflag:

```shell
--ysql_pg_conf_csv="ssl_min_protocol_version=TLSv1.2"
```
