---
title: Create a KMS configuration using AWS KMS
headerTitle: Create a KMS configuration using AWS KMS
linkTitle: Create a KMS configuration
description: Use YugabyteDB Anywhere to create a KMS configuration for Amazon Web Services (AWS) KMS.
menu:
  v2.14_yugabyte-platform:
    parent: security
    identifier: create-kms-config-1-aws-kms
    weight: 27
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./aws-kms.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      AWS KMS
    </a>
  </li>

  <li >
    <a href="{{< relref "./hashicorp-kms.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      HashiCorp Vault
    </a>
  </li>

</ul>

Encryption at rest uses universe keys to encrypt and decrypt universe data keys. You can use the YugabyteDB Anywhere UI to create key management service (KMS) configurations for generating the required universe keys for one or more YugabyteDB universes. Encryption at rest in YugabyteDB Anywhere supports the use of [Amazon Web Services (AWS) KMS](https://aws.amazon.com/kms/).

The AWS user associated with a KMS configuration requires the following minimum Identity and Access Management (IAM) KMS-related permissions:

- `kms:CreateKey`
- `kms:ListAliases`
- `kms:ListKeys`
- `kms:CreateAlias`
- `kms:DeleteAlias`
- `kms:UpdateAlias`
- `kms:TagResource`

You can create a KMS configuration that uses AWS KMS as follows:

1. Use the YugabyteDB Anywhere UI to navigate to **Configs > Security > Encryption At Rest** to access the list of existing configurations.

2. Click **Create New Config**.

3. Enter the following configuration details in the form:

    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **AWS KMS**.
    - **Use IAM Profile** — Specify whether or not to use an IAM profile attached to an Amazon Elastic Compute Cloud (EC2) instance running YugabyteDB. For more information, see [Using instance profiles](https://docs.aws.amazon.com/IAM/latest/UserGuide/id_roles_use_switch-role-ec2_instance-profiles.html).
    - **Access Key Id** — Enter the identifier for the access key.
    - **Secret Key Id** — Enter the identifier for the secret key.
    - **Region** — Select the AWS region where the customer master key (CMK) that was used for generating the universe keys is to be located. This setting does not need to match the region where the encrypted universe resides on AWS.
    - **Customer Master Key ID** — Enter the identifier for the CMK. If an identifier is not entered, a CMK ID will be auto-generated.
    - **AWS KMS Endpoint** — Specify the KMS endpoint to ensure that the encryption traffic is routed across your internal links without crossing into an external network.

4. Optionally, click **Upload CMK Policy** to select a custom policy file. The following is the default policy:

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

5. Click **Save**.<br>

    Your new configuration should appear in the list of configurations. A saved KMS configuration can only be deleted if it is not in use by any existing universes.

6. Optionally, to confirm that the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.
