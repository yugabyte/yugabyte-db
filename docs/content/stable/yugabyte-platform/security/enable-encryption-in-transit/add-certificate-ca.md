---
title: Add CA-signed certificates to YugabyteDB Anywhere
headerTitle: Add certificates
linkTitle: Add certificates
description: Add CA-signed certificates to YugabyteDB Anywhere.
headcontent: Use your own certificates for encryption in transit
menu:
  stable_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: add-certificate-2-ca
    weight: 20
type: docs
---

{{<tabs>}}
{{<tabitem href="../add-certificate-self/" text="Self-Signed" >}}
{{<tabitem href="../add-certificate-ca/" text="CA-Signed" active="true" >}}
{{<tabitem href="../add-certificate-hashicorp/" text="Hashicorp Vault" >}}
{{<tabitem href="../add-certificate-kubernetes/" text="Kubernetes cert-manager" >}}
{{</tabs>}}

For universes created with an on-premises provider, instead of using self-signed certificates, you can use third-party certificates from external certificate authorities (CA). The third-party CA root certificate must be configured in YugabyteDB Anywhere. You also have to copy the custom CA root certificate, node certificate, and node key to the appropriate on-premises provider nodes.

## Prerequisites

The server and CA certificates must adhere to the following criteria:

- Be stored in a `.crt` file, with both the certificate and the private key being in the PEM format.

  If your certificates and keys are stored in the PKCS12 format, you can [convert them to the PEM format](#convert-certificates-and-keys-from-pkcs12-to-pem-format).

The server certificates must adhere to the following criteria:

- Contain IP addresses of the database nodes in the Common Name or in the Subject Alternative Name. For on-premises universes where nodes are identified using DNS addresses, the server certificates should include the DNS names of the database nodes in the Common Name or Subject Alternate Name (wildcards are acceptable).

## Add CA-signed certificates

The following procedure describes how to install certificates on the database nodes. You have to repeat these steps for every database node that is to be used in the creation of a universe.

### Obtain certificates and keys

Obtain the keys and the custom CA-signed certificates for each of the on-premise nodes for which you are configuring node-to-node TLS. In addition, obtain the keys and the custom signed certificates for client access for configuring client-to-node TLS.

### Copy the certificates to each node

For each on-premises provider node, copy the custom CA certificate, node certificate, and node key to that node's file system.

If you are enabling client-to-node TLS, make sure to copy the client-facing server certificate and client-facing server key to each of the nodes.

In addition, ensure the following:

- The file names and file paths of different certificates and keys are identical across all the database nodes. For example, if you name your CA root certificate as `ca.crt` on one node, then you must name it `ca.crt` on all the nodes. Similarly, if you copy `ca.crt` to `/opt/yugabyte/keys` on one node, then you must copy `ca.crt` to the same path on other nodes.
- The `yugabyte` system user has read permissions to all the certificates and keys.

### Add the CA certificate to YugabyteDB Anywhere

Add a CA-signed certificate to YugabyteDB Anywhere as follows:

1. Navigate to **Configs > Security > Encryption in Transit**.

1. Click **Add Certificate** to open the **Add Certificate** dialog.

1. Select **CA Signed**, as per the following illustration:

    ![Add CA certificate](/images/yp/encryption-in-transit/add-cert.png)

1. In the **Certificate Name** field, enter a meaningful name for your certificate.

1. Upload the custom CA certificate (including any intermediate certificates in the chain) as the Root CA certificate.

    If you use an intermediate CA/issuer, but do not have the complete chain of certificates, then you need to create a bundle by executing the `cat intermediate-ca.crt root-ca.crt > bundle.crt` command, and then use this bundle as the root certificate. You might also want to [verify the certificate chain](#verify-certificate-chain).

1. Enter the file paths for each of the certificates on the nodes. These are the paths from the previous step.

1. Use the **Expiration Date** field to specify the expiration date of the certificate. To find this information, execute the `openssl x509 -in <root_crt_file_path> -text -noout` command and note the **Validity Not After** date.

1. Click **Add** to make the certificate available.

You can rotate certificates for universes configured with the same type of certificates. This involves replacing existing certificates with new database node certificates.

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

1. Verify that the node certificate Common Name (CN) or Subject Alternate Name  (SAN) contains the IP address or DNS name of each on-premises node on which the nodes are deployed.

    {{< note >}}
Each entry you provide for the CN or SAN must match the on-premises node as entered in the provider configuration. For example, if the node address is entered as a DNS address in the on-premises provider configuration, you must use the same DNS entry in the CN or SAN, not the resolved IP address.
    {{< /note >}}

    If you face any issue with the above verification, you can customize the level of certificate validation while creating a universe that uses these certificates. Refer to [Customizing the verification of RPC server certificate by the client](https://www.yugabyte.com/blog/yugabytedb-server-to-server-encryption/#customizing-the-verification-of-rpc-server-certificate-by-the-client).

{{< note >}}
The client certificates and keys are required only if you intend to use [PostgreSQL certificate-based authentication](https://www.postgresql.org/docs/current/auth-pg-hba-conf.html#:~:text=independent%20authentication%20option-,clientcert,-%2C%20which%20can%20be).
{{< /note >}}
