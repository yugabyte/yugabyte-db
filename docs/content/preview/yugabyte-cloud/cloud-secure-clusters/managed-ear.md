---
title: Encryption at rest
linkTitle: Encryption at rest
description: YugabyteDB Managed cluster encryption at rest.
headcontent: Encrypt your cluster database
menu:
  preview_yugabyte-cloud:
    identifier: managed-ear
    parent: cloud-secure-clusters
    weight: 460
type: docs
---

YugabyteDB Managed uses volume encryption for all data at rest, including your account data and your clusters and their backups. Data is AES-256 encrypted using native cloud provider technologies - S3 and EBS volume encryption for AWS, and server-side and persistent disk encryption for GCP. Volume encryption keys are managed by the cloud provider and anchored by hardware security appliances.

In addition to basic volume encryption, you can enable database-level encryption at rest (EAR) for clusters. When enabled, your YugabyteDB database (including backups) is encrypted using a customer managed key (CMK) residing in a cloud provider Key Management Service (KMS). (Currently, only AWS KMS is supported, for clusters deployed in AWS.) You can grant YugabyteDB Managed access to the key with the requisite permissions to perform cryptographic operations using the key to secure the databases in your clusters.

Cluster-level encryption can be configured as follows:

- On the **Security** page of the **Create Cluster** wizard when you create a cluster.
- On the cluster **Settings** tab under **Encryption at rest**.

You must be signed in as an Admin user to manage cluster-level EAR.

## Limitations

- Currently, cluster-level EAR is only supported for clusters deployed in AWS and using AWS KMS.
- You cannot remove encryption from clusters that have cluster-level EAR enabled.
- After cluster-level EAR is enabled, you cannot change keys.

## Prerequisites

### AWS

- CMK created in AWS KMS.
- Amazon Resource Name (ARN) of the CMK.
- An access key for an [IAM identity](https://docs.aws.amazon.com/IAM/latest/UserGuide/id.html) with permissions for the CMK. An access key consists of an access key ID and the secret access key. For more information, refer to [Managing access keys for IAM users](https://docs.aws.amazon.com/IAM/latest/UserGuide/id_credentials_access-keys.html) in the AWS documentation.

For more information on AWS KMS, refer to [AWS Key Management Service](https://docs.aws.amazon.com/kms/) in the AWS documentation.

## Encrypt a cluster

You can enable database EAR for clusters deployed in AWS as follows:

1. On the cluster **Settings** tab, select **Encryption at rest**.
1. Click **Enable Encryption**.
1. Enter the ARN of the CMK to use to encrypt the cluster database.
1. Enter the Access key of an IAM identity with permissions for the CMK. An access key consists of an access key ID and the secret access key.
1. Click **Encrypt**.

YugabyteDB Managed validates the key and, if successful, starts encrypting the database.
