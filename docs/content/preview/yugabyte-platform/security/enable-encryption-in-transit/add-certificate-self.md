---
title: Add self-signed certificates to YugabyteDB Anywhere
headerTitle: Add certificates
linkTitle: Add certificates
description: Add self-signed certificates to YugabyteDB Anywhere.
headcontent: Add self-signed certificates to YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: add-certificate-1-self
    weight: 20
type: docs
---

{{<tabs>}}
{{<tabitem href="../add-certificate-self/" text="Self-Signed" active="true" >}}
{{<tabitem href="../add-certificate-ca/" text="CA-Signed" >}}
{{<tabitem href="../add-certificate-hashicorp/" text="Hashicorp Vault" >}}
{{<tabitem href="../add-certificate-kubernetes/" text="Kubernetes" >}}
{{</tabs>}}

Instead of using YugabyteDB Anywhere-provided certificates, you can use your own self-signed certificates that you upload to YugabyteDB Anywhere.

## Prerequisites

The certificates must meet the following criteria:

- Be in the `.crt` format and the private key must be in the `.pem` format, with both of these artifacts available for upload.
- Contain IP addresses of the target database nodes or DNS names as the Subject Alternative Names (wildcards are acceptable).

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

### Verify certificate chain

Perform the following steps to verify your certificates:

1. Execute the following verify command which checks the database node certificate (node.crt) against the root CA certificate (ca.crt):

    ```sh
    openssl verify ca.crt node.crt
    ```

1. Verify that the node certificate (`node.crt`) and the node private key (`node.key`) match. See [How do I verify that a private key matches a certificate?](https://www.ssl247.com/knowledge-base/detail/how-do-i-verify-that-a-private-key-matches-a-certificate-openssl-1527076112539/ka03l0000015hscaay/)

1. Verify that the node certificate and Root CA certificate expiration is at least 3 months by checking the validity field in the output of the following commands:

    ```sh
    openssl x509 -in node.crt -text -noout
    ```

    ```sh
    openssl x509 -in ca.crt -text -noout
    ```

1. Verify that the node certificate Common Name (CN) or Subject Alternate Name  (SAN) contains the IP address or DNS name of each on-prem node on which the nodes are deployed.

    {{< note >}}
Each entry you provide for the CN or SAN must match the on-prem node as entered in the provider configuration. For example, if the node address is entered as a DNS address in the on-prem provider configuration, you must use the same DNS entry in the CN or SAN, not the resolved IP address.
    {{< /note >}}

    If you face any issue with the above verification, you can customize the level of certificate validation while creating a universe that uses these certificates. Refer to [Customizing the verification of RPC server certificate by the client](https://www.yugabyte.com/blog/yugabytedb-server-to-server-encryption/#customizing-the-verification-of-rpc-server-certificate-by-the-client).

{{< note >}}
The client certificates and keys are required only if you intend to use [PostgreSQL certificate-based authentication](https://www.postgresql.org/docs/current/auth-pg-hba-conf.html#:~:text=independent%20authentication%20option-,clientcert,-%2C%20which%20can%20be).
{{< /note >}}

## Add self-signed certificates

To add self-signed certificates to YugabyteDB Anywhere:

1. Navigate to **Configs > Security > Encryption in Transit**.
1. Click **Add Certificate** to open the **Add Certificate** dialog.
1. Select **Self Signed**.
1. Click **Upload Root Certificate**, then browse to the root certificate file (`<file-name>.crt`) and upload it.
1. Click **Upload Key**, then browse to the root certificate file (`<file-name>.key`) and upload it.
1. In the **Certificate Name** field, enter a meaningful name for your certificate.
1. In the **Expiration Date** field, specify the expiration date of the root certificate. To find this information, execute the `openssl x509 -in <root-crt-file-path> -text -noout` command and note the **Validity Not After** date.
1. Click **Add** to make the certificate available.
