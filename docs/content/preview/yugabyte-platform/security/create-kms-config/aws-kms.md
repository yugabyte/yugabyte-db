---
title: Create a KMS configuration using AWS KMS
headerTitle: Create a KMS configuration
linkTitle: Create a KMS configuration
description: Use YugabyteDB Anywhere to create a KMS configuration for Amazon Web Services (AWS) KMS.
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: create-kms-config-1-aws-kms
    weight: 50
type: docs
---

Encryption at rest uses a master key to encrypt and decrypt universe keys. The master key details are stored in YugabyteDB Anywhere in key management service (KMS) configurations. You enable encryption at rest for a universe by assigning the universe a KMS configuration. The master key designated in the configuration is then used for generating the universe keys used for encrypting the universe data.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../aws-kms/" class="nav-link active">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS KMS
    </a>
  </li>
  <li >
    <a href="../google-kms/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      Google KMS
    </a>
  </li>

  <li >
    <a href="../azure-kms/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure Key Vault
    </a>
  </li>

  <li >
    <a href="../hashicorp-kms/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      HashiCorp Vault
    </a>
  </li>

</ul>

Encryption at rest in YugabyteDB Anywhere supports the use of [Amazon Web Services (AWS) KMS](https://aws.amazon.com/kms/).

## Prerequisites

The master key resource policy and AWS user associated with a KMS configuration require specific permissions. Refer to [To use encryption at rest with YugabyteDB Anywhere](../../../prepare/cloud-permissions/cloud-permissions-ear/).

## Create a KMS configuration

You can create a KMS configuration that uses AWS KMS, as follows:

1. Use the YugabyteDB Anywhere UI to navigate to **Configs > Security > Encryption At Rest** to access the list of existing configurations.

1. Click **Create New Config**.

1. Enter the following configuration details in the form:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **AWS KMS**.
    - **Use IAM Profile** — Specify whether or not to use an IAM profile attached to an Amazon Elastic Compute Cloud (EC2) instance running YugabyteDB. For more information, see [Using instance profiles](https://docs.aws.amazon.com/IAM/latest/UserGuide/id_roles_use_switch-role-ec2_instance-profiles.html).
    - **Access Key Id** — Enter the identifier for the access key.
    - **Secret Key Id** — Enter the identifier for the secret key.
    - **Region** — Select the AWS region where the customer master key (CMK) that was used for generating the universe keys is to be located. This setting does not need to match the region where the encrypted universe resides on AWS.
    - **Customer Master Key ID** — Enter the identifier for the CMK. If an identifier is not entered, a CMK ID will be auto-generated.
    - **AWS KMS Endpoint** — Specify the KMS endpoint to ensure that the encryption traffic is routed across your internal links without crossing into an external network.

1. Optionally, click **Upload CMK Policy** to select a custom policy file. The following is the default policy:

    ```json
      {
          "Version": "2012-10-17",
          "Id": "key-default-1",
          "Statement": [
              {
                  "Sid": "Enable IAM User Permissions",
                  "Effect": "Allow",
                  "Principal": {
                      "AWS": "arn:aws:iam::<AWS_ACCOUNT_ID>:root"
                  },
                  "Action": "kms:*",
                  "Resource": "*"
              },
              {
                  "Sid": "Allow access for Key Administrators",
                  "Effect": "Allow",
                  "Principal": {
                      "AWS": "arn:aws:iam::<AWS_ACCOUNT_ID>:[user|role]{1}/[<USER_NAME>|<ROLE_NAME>]{1}"
                  },
                  "Action": "kms:*",
                  "Resource": "*"
              }
          ]
      }
    ```

1. Click **Save**.

    Your new configuration should appear in the list of configurations. A saved KMS configuration can only be deleted if it is not in use by any existing universes.

1. Optionally, to confirm that the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.

## Modify a KMS configuration

You can modify an existing KMS configuration as follows:

1. Using the YugabyteDB Anywhere UI, navigate to **Configs > Security > Encryption At Rest** to open a list of existing configurations.

1. Find the configuration you want to modify and click its corresponding **Actions > Edit Configuration**.

1. Provide new values for the **Vault Address** and **Secret Token** fields.

1. Click **Save**.

1. Optionally, to confirm that the information is correct, click **Show details** or **Actions > Details**.

## Delete a KMS configuration

{{<note title="Note">}}
You can only delete a KMS configuration if it has never been used by any universes.
{{</note>}}

To delete a KMS configuration, click its corresponding **Actions > Delete Configuration**.
