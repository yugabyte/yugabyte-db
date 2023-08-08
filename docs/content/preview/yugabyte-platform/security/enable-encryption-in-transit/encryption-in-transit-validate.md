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
