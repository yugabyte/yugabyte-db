---
title: Add CA-signed certificates to YugabyteDB Anywhere
headerTitle: Add certificates
linkTitle: Add certificates
description: Add CA-signed certificates to YugabyteDB Anywhere.
headcontent: Add CA-signed certificates to YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: add-certificate-2-ca
    weight: 20
type: docs
---

{{<tabs>}}
{{<tabitem href="../add-certificate-self/" text="Self-Signed" >}}
{{<tabitem href="../add-certificate-ca/" text="CA-Signed" active="true" >}}
{{<tabitem href="../add-certificate-hashicorp/" text="Hashicorp Vault" >}}
{{<tabitem href="../add-certificate-kubernetes/" text="Kubernetes" >}}
{{</tabs>}}

For universes created with an on-premises provider, instead of using self-signed certificates, you can use third-party certificates from external certificate authorities (CA). The third-party CA root certificate must be configured in YugabyteDB Anywhere. You also have to copy the custom CA root certificate, node certificate, and node key to the appropriate on-premises provider nodes.

## Prerequisites

The certificates must adhere to the following criteria:

- Be stored in a `.crt` file, with both the certificate and the private key being in the PEM format.

  If your certificates and keys are stored in the PKCS12 format, you can [convert them to the PEM format](#convert-certificates-and-keys-from-pkcs12-to-pem-format).

- Contain IP addresses of the database nodes or DNS names as the Subject Alternative Names (wildcards are acceptable).

## Add CA-signed certificates

The following procedure describes how to install certificates on the database nodes. You have to repeat these steps for every database node that is to be used in the creation of a universe.

### Obtain certificates and keys

Obtain the keys and the custom CA-signed certificates for each of the on-premise nodes for which you are configuring node-to-node TLS. In addition, obtain the keys and the custom signed certificates for client access for configuring client-to-node TLS.

### Copy the certificates to each node

For each on-premises provider node, copy the custom CA root certificate, node certificate, and node key to that node's file system.

If you are enabling client-to-node TLS, make sure to copy the client certificate and client key to each of the nodes.

In addition, ensure the following:

- The file names and file paths of different certificates and keys are identical across all the database nodes. For example, if you name your CA root certificate as `ca.crt` on one node, then you must name it `ca.crt` on all the nodes. Similarly, if you copy `ca.crt` to `/opt/yugabyte/keys` on one node, then you must copy `ca.crt` to the same path on other nodes.
- The yugabyte system user has read permissions to all the certificates and keys.

### Add the CA certificate to YugabyteDB Anywhere

Add a CA-signed certificate in YugabyteDB Anywhere, as follows:

1. Navigate to **Configs > Security > Encryption in Transit**.

1. Click **Add Certificate** to open the **Add Certificate** dialog.

1. Select **CA Signed**, as per the following illustration:

    ![add-cert](/images/yp/encryption-in-transit/add-cert.png)

1. Upload the custom CA root certificate as the root certificate.

    If you use an intermediate CA/issuer, but do not have the complete chain of certificates, then you need to create a bundle by executing the `cat intermediate-ca.crt root-ca.crt > bundle.crt` command, and then use this bundle as the root certificate. You might also want to [verify the certificate chain](#verify-certificate-chain).

1. Enter the file paths for each of the certificates on the nodes. These are the paths from the previous step.

1. In the **Certificate Name** field, enter a meaningful name for your certificate.

1. Use the **Expiration Date** field to specify the expiration date of the certificate. To find this information, execute the `openssl x509 -in <root_crt_file_path> -text -noout` command and note the **Validity Not After** date.

1. Click **Add** to make the certificate available.

You can rotate certificates for universes configured with the same type of certificates. This involves replacing existing certificates with new database node certificates.
