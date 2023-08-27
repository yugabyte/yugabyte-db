---
title: Encryption at rest
linkTitle: Encryption at rest
description: YugabyteDB Managed cluster encryption at rest.
headcontent: Encrypt your YugabyteDB cluster
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview_yugabyte-cloud:
    identifier: managed-ear
    parent: cloud-secure-clusters
    weight: 460
type: docs
---

For added security, you can encrypt your clusters (including backups) using a customer managed key (CMK) residing in a cloud provider Key Management Service (KMS). You grant YugabyteDB Managed access to the key with the requisite permissions to perform cryptographic operations using the key to secure the databases in your clusters.

You enable YugabyteDB cluster encryption at rest (EAR) when you create it. See [Create your cluster](../../cloud-basics/create-clusters/).

<!-- You can enable YugabyteDB EAR for a cluster as follows:

- On the **Security** page of the **Create Cluster** wizard when you create a cluster.
- On the cluster **Settings** tab under **Encryption at rest**.
-->

Note that, regardless of whether you enable YugabyteDB EAR for a cluster, YugabyteDB Managed uses volume encryption for all data at rest, including your account data, your clusters, and their backups. Data is AES-256 encrypted using native cloud provider technologies - S3 and EBS volume encryption for AWS, and server-side and persistent disk encryption for GCP. Volume encryption keys are managed by the cloud provider and anchored by hardware security appliances.

## Limitations

- Currently, you cannot enable cluster EAR for existing clusters.
- You cannot remove encryption from clusters that have EAR enabled.
- After EAR is enabled for a cluster, you cannot change keys.

Enabling EAR can impact cluster performance. You should monitor your workload after enabling this feature.

## Prerequisites

{{< tabpane text=true >}}

  {{% tab header="AWS" lang="aws" %}}

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

  {{% tab header="GCP" lang="gcp" %}}

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

<!--## Encrypt a cluster

You can enable EAR for clusters in AWS as follows:

1. On the cluster **Settings** tab, select **Encryption at rest**.
1. Click **Enable Encryption**.
1. Enter the ARN of the AWS CMK to use to encrypt the cluster.
1. Enter the Access key of an IAM identity with permissions for the CMK. An access key consists of an **Access key ID** and the **Secret access key**. You would have saved the secret access key to a secure location when you created the access key in AWS.
1. Click **Encrypt**.

YugabyteDB Managed validates the key and, if successful, starts encrypting the data. Only new data is encrypted with the new key. Old data remains unencrypted until compaction churn triggers a re-encryption with the new key.
-->
