---
title: Encryption at rest in YugabyteDB Aeon
headerTitle: Encryption at rest
linkTitle: Encryption at rest
description: YugabyteDB Aeon cluster encryption at rest.
headcontent: Encrypt your YugabyteDB cluster
menu:
  stable_yugabyte-cloud:
    identifier: managed-ear
    parent: cloud-secure-clusters
    weight: 460
type: docs
---

{{< page-finder/head text="Encryption at rest" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../../secure/encryption-at-rest/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../../yugabyte-platform/security/enable-encryption-at-rest/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" current="" >}}
{{< /page-finder/head >}}

For added security, you can encrypt your clusters (including backups) using a customer managed key (CMK) residing in a cloud provider Key Management Service (KMS). You grant YugabyteDB Aeon access to the key with the requisite permissions to perform cryptographic operations using the key to secure the databases in your clusters.

You can enable YugabyteDB EAR for a cluster as follows:

- On the **Security** page of the **Create Cluster** wizard when you [create your cluster](../../cloud-basics/create-clusters/).
- On the cluster **Settings** tab under **Encryption at rest** (database version 2.16.7 and later only).

Note that, regardless of whether you enable YugabyteDB EAR for a cluster, YugabyteDB Aeon uses volume encryption for all data at rest, including your account data, your clusters, and their backups. Data is AES-256 encrypted using native cloud provider technologies - S3 and EBS volume encryption for AWS, Azure disk encryption, and server-side and persistent disk encryption for GCP. Volume encryption keys are managed by the cloud provider and anchored by hardware security appliances.

## How the CMK encrypts the cluster

The CMK you create in your cloud provider KMS does not encrypt cluster data directly. The CMK is the master key. It wraps each universe (cluster) key. YugabyteDB Aeon keeps those wrapped copies, and backup metadata includes them.

A universe key protects data written to disk by encrypting the key for each data file. The cluster keeps a registry of universe keys, encrypted with the latest universe key. YugabyteDB Aeon generates the universe keys. You do not create them in AWS, Azure, or GCP. For the full key hierarchy, refer to [Encryption at rest in YugabyteDB Anywhere](../../../yugabyte-platform/security/enable-encryption-at-rest/). (Note that Anywhere uses the same key names and calls a cluster a universe.)

## Limitations

- You can't enable cluster EAR on clusters with YugabyteDB versions earlier than 2.16.7.

- Enabling EAR can impact cluster performance. You should monitor your workload after enabling this feature.

## Prerequisites

{{< tabpane text=true >}}

  {{% tab header="AWS" lang="aws" %}}

To use AWS KMS, you need the following:

- Single-region [symmetric encryption key](https://docs.aws.amazon.com/kms/latest/developerguide/concepts.html#symmetric-cmks) created in AWS KMS. The key resource policy should include the following [actions](https://docs.aws.amazon.com/kms/latest/developerguide/key-policy-default.html#key-policy-users-crypto):
  - kms:Encrypt
  - kms:Decrypt
  - kms:GenerateDataKeyWithoutPlaintext
  - kms:DescribeKey
  - kms:ListAliases
- Amazon Resource Name (ARN) of the CMK. For more information, refer to [Amazon Resource Names](https://docs.aws.amazon.com/IAM/latest/UserGuide/reference-arns.html) in the AWS documentation.
- An access key for an [IAM identity](https://docs.aws.amazon.com/IAM/latest/UserGuide/id.html) with permission to encrypt and decrypt using the CMK. An access key consists of an access key ID and the secret access key. For more information, refer to [Managing access keys for IAM users](https://docs.aws.amazon.com/IAM/latest/UserGuide/id_credentials_access-keys.html) in the AWS documentation.

For more information on AWS KMS, refer to [AWS Key Management Service](https://docs.aws.amazon.com/kms/) in the AWS documentation.

  {{% /tab %}}

  {{% tab header="Azure" lang="azure" %}}

Create a key vault using the [Azure portal](https://docs.microsoft.com/en-us/azure/key-vault/general/quick-create-portal). The following settings are required:

- Set the vault permission model as Vault access policy.
- Add the application to the key vault access policies with the minimum key management operations permissions of Get and Create (unless you are pre-creating the key), as well as cryptographic operations permissions of Unwrap Key and Wrap Key.

    Required permissions are as follows:

    ```json
    "permissions": [ {
        "actions": [
          "Microsoft.KeyVault/vaults/keys/read",
          "Microsoft.KeyVault/vaults/read"
        ],
        "notActions": [],
        "dataActions": [
          "Microsoft.KeyVault/vaults/keys/read",
          "Microsoft.KeyVault/vaults/keyrotationpolicies/read",
          "Microsoft.KeyVault/vaults/keys/wrap/action",
          "Microsoft.KeyVault/vaults/keys/unwrap/action"
        ],
        "notDataActions": []
    } ]
    ```

If you are planning to use an existing cryptographic key with the same name, it must meet the following criteria:

- The primary key version should be in the Enabled state.
- The activation date should either be disabled or set to a date before the KMS configuration creation.
- The expiration date should be disabled.
- Permitted operations should have at least WRAP_KEY and UNWRAP_KEY.
- The key rotation policy should not be defined in order to avoid automatic rotation.

In addition, you need the client ID and secret for an application registered in Azure with permission to encrypt and decrypt using the CMK. Refer to [Create a new client secret](https://learn.microsoft.com/en-us/entra/identity-platform/howto-create-service-principal-portal#option-3-create-a-new-client-secret) in the Microsoft documentation.

For more information, refer to the [Azure Key Vault documentation](https://learn.microsoft.com/en-us/azure/key-vault/general/).

  {{% /tab %}}

  {{% tab header="GCP" lang="gcp" %}}

To use Cloud KMS, you need the following:

- CMK (AKA customer-managed encryption key or CMEK) created in Cloud KMS.
- Cloud KMS resource ID. You can copy the resource ID from KMS Management page in the Google Cloud console. Do not include the key version. For more information, refer to [Getting a Cloud KMS resource ID](https://cloud.google.com/kms/docs/getting-resource-ids) in the GCP documentation.
- A service account that has been granted the following permissions on the CMK:
  - cloudkms.keyRings.get
  - cloudkms.cryptoKeys.get
  - cloudkms.cryptoKeyVersions.useToEncrypt
  - cloudkms.cryptoKeyVersions.useToDecrypt
  - cloudkms.locations.generateRandomBytes
- Service account credentials. These credentials are used to authorize your use of the CMK. This is the key file (JSON) that you downloaded when creating credentials for the service account. For more information, refer to [Create credentials for a service account](https://developers.google.com/workspace/guides/create-credentials#create_credentials_for_a_service_account) in the GCP documentation.

For more information on GCP KMS, refer to [Cloud Key Management Service overview](https://cloud.google.com/kms/docs/key-management-service/) in the GCP documentation.

  {{% /tab %}}

{{< /tabpane >}}

## Encrypt a cluster using a CMK

{{< tabpane text=true >}}

  {{% tab header="AWS" lang="aws" %}}

You can enable EAR using a CMK for clusters (database version 2.16.7 and later only) as follows:

1. On the cluster **Settings** tab, select **Encryption at rest**.
1. Click **Enable Cluster Encryption at Rest**.
1. Enter the Amazon Resource Name (ARN) of the CMK to use to encrypt the cluster.
1. Provide an access key of an [IAM identity](https://docs.aws.amazon.com/IAM/latest/UserGuide/id.html) with permissions for the CMK. An access key consists of an access key ID and the secret access key.

  {{% /tab %}}

  {{% tab header="Azure" lang="azure" %}}

You can enable EAR using a CMK for clusters (database version 2.16.7 and later only) as follows:

1. On the cluster **Settings** tab, select **Encryption at rest**.
1. Click **Enable Cluster Encryption at Rest**.
1. Provide the Azure [tenant ID](https://learn.microsoft.com/en-us/entra/fundamentals/how-to-find-tenant), the vault URI (for example, `https://myvault.vault.azure.net`), and the name of the key.
1. Enter the client ID and secret for an application with permission to encrypt and decrypt using the CMK.

  {{% /tab %}}

  {{% tab header="GCP" lang="gcp" %}}

You can enable EAR using a CMK for clusters (database version 2.16.7 and later only) as follows:

1. On the cluster **Settings** tab, select **Encryption at rest**.
1. Click **Enable Cluster Encryption at Rest**.
1. Enter the resource ID of the key ring where the CMK is stored.
1. Click **Add Key** to select the credentials JSON file you downloaded when creating credentials for the service account that has permissions to encrypt and decrypt using the CMK.

  {{% /tab %}}

{{< /tabpane >}}

Click **Save** when you are done.

YugabyteDB Aeon validates the CMK and, if successful, generates a universe key and starts encrypting the data. Only new data is encrypted. Existing data remains unencrypted until compaction rewrites it under that universe key. You cannot see what fraction of existing data has been rewritten. To force a full rewrite, contact {{% support-cloud %}}.

To disable cluster EAR, click **Disable Encryption at Rest**. YugabyteDB Aeon uses lazy decryption to decrypt the cluster.

## Rotate your CMK

When you edit the CMK configuration, YugabyteDB Aeon rotates the master key (your CMK) only. The existing universe keys stay in place. See [How the CMK encrypts the cluster](#how-the-cmk-encrypts-the-cluster).

{{< warning title="Deleting your CMK" >}}
Deleting the CMK that is currently configured makes YugabyteDB Aeon unable to unwrap the universe keys it stores for the cluster, including the copies recorded in backup metadata.

You can remove a previous CMK after both of the following are true:

- Incremental backups taken before the rotation have aged out, so they can no longer be restored. The same applies to any full backup from before the rotation that you still need.
- You no longer need [point-in-time recovery (PITR)](../cloud-clusters/aeon-pitr/) to a time before the rotation.

If you must be able to restore a backup for 30 days, keep the rotated CMK for those 30 days.
{{< /warning >}}

To rotate the CMK used for EAR, do the following:

1. On the cluster **Settings** tab, select **Encryption at rest**.
1. Click **Edit CMK Configuration**.
1. For AWS, provide the following details:

    - **Customer managed key (CMK)**: Enter the Amazon Resource Name (ARN) of the new CMK to use to encrypt the cluster.
    - **Access key**: Provide an access key of an [IAM identity](https://docs.aws.amazon.com/IAM/latest/UserGuide/id.html) with permissions for the CMK. An access key consists of an access key ID and the secret access key.

    For Azure, provide the following details:

    - Provide the Azure [tenant ID](https://learn.microsoft.com/en-us/entra/fundamentals/how-to-find-tenant), the vault URI (for example, `https://myvault.vault.azure.net`), and the name of the new key.
    - Enter the client ID and secret for the application with permission to encrypt and decrypt using the CMK.

    For GCP:
    - **Resource ID**: Enter the resource ID of the key ring where the new CMK is stored.
    - **Service Account Credentials**: Click **Add Key** to select the credentials JSON file you downloaded when creating credentials for the service account that has permissions to encrypt and decrypt using the CMK.

1. Click **Save**.

YugabyteDB Aeon then does the following:

- Every universe key that YugabyteDB Aeon stores for the cluster is re-wrapped with the new CMK immediately. No new universe key is generated. Backups taken after the rotation record those newly wrapped keys. The cluster's universe key registry and the data on disk stay as they are.
- The previous CMK is still required to restore backups and PITR history from before the rotation.
