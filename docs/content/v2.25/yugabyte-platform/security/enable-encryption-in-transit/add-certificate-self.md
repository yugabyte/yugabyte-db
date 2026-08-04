---
title: Add self-signed certificates to YugabyteDB Anywhere
headerTitle: Add certificates
linkTitle: Add certificates
description: Add self-signed certificates to YugabyteDB Anywhere.
headcontent: Use your own certificates for encryption in transit
menu:
  v2.25_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: add-certificate-1-self
    weight: 20
type: docs
---

{{<tabs>}}
{{<tabitem href="../add-certificate-self/" text="Self-Signed" active="true" >}}
{{<tabitem href="../add-certificate-ca/" text="CA-Signed" >}}
{{<tabitem href="../add-certificate-hashicorp/" text="Hashicorp Vault" >}}
{{<tabitem href="../add-certificate-kubernetes/" text="Kubernetes cert-manager" >}}
{{</tabs>}}

Instead of using YugabyteDB Anywhere-provided certificates, you can use your own self-signed certificates that you upload to YugabyteDB Anywhere.

## Prerequisites

The certificate and private key must be in PEM format.

YugabyteDB Anywhere produces the node (leaf) certificates from the uploaded certificates and copies the certificate chain, leaf certificate, and private key to the nodes in the cluster.

### Convert certificates and keys from PKCS12 to PEM format

If your certificates and keys are stored in the PKCS12 format, you can convert them to the PEM format using OpenSSL.

Start by extracting the certificate via the following command:

```sh
openssl pkcs12 -in cert-archive.pfx -out cert.pem -clcerts -nokeys
```

To extract the key and write it to the PEM file unencrypted, execute the following command:

```sh
openssl pkcs12 -in cert-archive.pfx -out key.pem -nocerts -nodes
```

If the key is protected by a passphrase in the PKCS12 archive, you are prompted for the passphrase.

### Generate self-signed certificates

You can generate self-signed certificates using openssl as follows:

```sh
openssl req -newkey rsa:2048 -nodes -keyout yugabyte_private_key.pem -x509 -days 365 -out yugabyte_cert.pem
```

See also [Generate the root certificate file](../../../../secure/tls-encryption/server-certificates/#generate-the-root-certificate-file).

## Add self-signed certificates

To add self-signed certificates to YugabyteDB Anywhere:

1. Navigate to **Integrations > Security > Encryption in Transit**.

1. Click **Add Certificate** to open the **Add Certificate** dialog.

1. Select **Self Signed**.

    ![Add Self Signed certificate](/images/yp/encryption-in-transit/add-self-cert.png)

1. In the **Certificate Name** field, enter a meaningful name for your certificate.

1. Click **Upload Root Certificate**, then browse to the root certificate file and upload it.

1. Click **Upload Key**, then browse to the private key and upload it.

1. In the **Expiration Date** field, specify the expiration date of the root certificate. To find this information, execute the `openssl x509 -in <root-crt-file-path> -text -noout` command and note the **Validity Not After** date.

1. Click **Add** to make the certificate available.

## Validate certificates

When configuring and using certificates, SSL issues may occasionally arise. You can validate your certificates and keys as follows:

- Verify that the CA CRT and CA private key match by executing the following commands:

    ```shell
    openssl rsa -noout -modulus -in ca.key | openssl md5
    openssl x509 -noout -modulus -in ca.crt | openssl md5

    \# outputs should match
    ```

- Verify that the CA CRT is actually a certificate authority by executing the following command:

    ```shell
    openssl x509 -text -noout -in ca.crt

    \# Look for fields

    X509v3 Basic Constraints:

      CA:TRUE
    ```

- Verify that certificates and keys are in PEM format (as opposed to the DER or other format). If these artifacts are not in the PEM format and you require assistance with converting them or identifying the format, consult [Converting certificates](https://support.globalsign.com/ssl/ssl-certificates-installation/converting-certificates-openssl).

- Ensure that the private key does not have a passphrase associated with it. For information on how to identify this condition, see [Decrypt an encrypted SSL RSA private key](https://techjourney.net/how-to-decrypt-an-enrypted-ssl-rsa-private-key-pem-key/).
