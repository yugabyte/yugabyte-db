---
title: Federated IAM backups
headerTitle: Federated IAM backups
linkTitle: Federated IAM backups
description: Back up YugabyteDB Anywhere universes to another cloud using each node's own identity.
headContent: Back up to another cloud using each database node's own identity
menu:
  stable_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: federated-iam-backups
    weight: 12
type: docs
---

Using federated IAM backups, you can back up from one cloud to a bucket on a different cloud. Database nodes on AWS or Google Cloud back up to a bucket in the other cloud using each node's own identity. YugabyteDB Anywhere and the nodes store no access keys or other secrets.

For a bucket in the same cloud as the nodes, follow [Configure backup storage](../configure-backup-storage/). Federated IAM applies only when backing up to the other cloud. [Identity the database nodes use](#identity-the-database-nodes-use) lists the identity those nodes use.

| Node runs on | Bucket | Mechanism |
| :--- | :--- | :--- |
| AWS | Google Cloud Storage | Google workload identity federation. The node uses its IAM role. |
| GCP | Amazon S3 | AWS `AssumeRoleWithWebIdentity`. The node uses its service account. |
| AWS or GCP | Same cloud | The credentials on the storage configuration. |

The database nodes read and write the backup data in the bucket. YugabyteDB Anywhere deletes backups and validates the storage configuration. Configure cross-cloud access in each of these places:

- **Provider.** To let the nodes reach the other cloud, edit the [provider configuration](#configure-the-provider). Under **Access to other clouds** (or **Cross Cloud Federated Access** on an on-premises provider), turn on the storage cloud under **Services**.
- **Storage configuration.** On the [storage configuration](#configure-backup-storage), turn **Federated IAM** on if YugabyteDB Anywhere is running on a different cloud from the bucket. That lets YugabyteDB Anywhere delete backups and validate the configuration. If YugabyteDB Anywhere is running on the same cloud as the bucket, leave the option off.
- **Universe.** YugabyteDB Anywhere applies the provider settings when you create a universe on a provider that has the other cloud turned on. On a universe that already exists, select **Actions > Edit Cross-Cloud Federated Access** and turn on **Enable Federated Access for Backups**. Without that setting, backups on the universe do not use the other cloud. Refer to [Create or update the universe](#create-or-update-the-universe).

## Limitations

- VM-based universes on AWS, Google Cloud, and on-premises providers. Kubernetes universes aren't supported.
- One provider per universe. A universe whose nodes use more than one provider can't enable federated IAM.
- On an on-premises provider, federated IAM is configured on the AWS and Google Cloud virtual machines. A bare-metal node is skipped, and the universe task continues.
- A bucket in the same cloud as the nodes uses the credentials on the storage configuration. With **IAM Role**, a node running on AWS uses temporary credentials from the instance profile of the instance where YugabyteDB Anywhere is running, unless **Use S3 IAM roles attached to DB node for Backup/Restore** (config key `yb.backup.s3.use_db_nodes_iam_role_for_backup`) is true. A node running on GCP that backs up to S3 needs that option set to true when YugabyteDB Anywhere is running on AWS.
- The AWS role must be a commercial `arn:aws:iam::` role. AWS China and AWS GovCloud aren't supported.

## Enable the feature

The Cross-Cloud Federated IAM feature is not available by default. To turn it on, set the **Enable Cross-Cloud Federated IAM** Global Runtime Configuration option (config key `yb.ui.feature_flags.enable_cross_cloud_federated_iam`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). You must be a Super Admin to set global runtime configuration flags. A provider or storage configuration that already has federation set keeps working if you turn the option off.

## Prepare Google Cloud Storage for AWS nodes

Do this in the Google Cloud project that owns the bucket. On the bucket, grant `roles/storage.objectAdmin` and `roles/storage.bucketViewer` to the IAM role already attached to each database VM. In the binding, use the role name, such as `yb-node-role`, not the full ARN.

`roles/storage.objectAdmin` lets the node create, read, list, and delete objects. `roles/storage.bucketViewer` is also required. Before a backup starts, the node reads the bucket metadata, and that call needs `storage.buckets.get`, which `roles/storage.objectAdmin` does not include.

If YugabyteDB Anywhere is also running on AWS, grant both roles to the IAM role attached to that instance. That role is separate from the role on the database nodes.

### Create a bucket with uniform access

Uniform bucket-level access must be on. Google Cloud rejects federated tokens against a bucket that still uses legacy access control lists.

```sh
gcloud storage buckets create gs://<GCS_BUCKET> --uniform-bucket-level-access

gcloud storage buckets describe gs://<GCS_BUCKET> \
  --format="value(uniform_bucket_level_access)"
```

The describe command prints `True`.

### Create a workload identity pool

An AWS node has no Google Cloud identity. The pool lets Google Cloud recognize the IAM role on that node. You grant that role access to the bucket, and you upload the pool's credential configuration file in YugabyteDB Anywhere.

```sh
gcloud iam workload-identity-pools create <POOL_NAME> \
  --project=<GCP_PROJECT> --location=global

gcloud iam workload-identity-pools providers create-aws <PROVIDER_NAME> \
  --project=<GCP_PROJECT> --location=global \
  --workload-identity-pool=<POOL_NAME> \
  --account-id=<AWS_ACCOUNT_ID> \
  --attribute-mapping="google.subject=assertion.arn,attribute.aws_role=assertion.arn.extract('assumed-role/{role}/')"
```

`<GCP_PROJECT>` is the project ID. `<GCP_PROJECT_NUMBER>` is the numeric project number, which is different from the project ID. `<AWS_ACCOUNT_ID>` is the 12-digit account that owns the node role.


In the Google Cloud console, open the workload identity pool and download the credential configuration file for the AWS provider you just created, `<PROVIDER_NAME>`. The file is JSON with `"type": "external_account"` and contains no key. Download it without service account impersonation. When you upload it in YugabyteDB Anywhere, the browser reads the audience and the file is not sent to YugabyteDB Anywhere. A file that impersonates a service account is rejected. The audience looks like `//iam.googleapis.com/projects/<GCP_PROJECT_NUMBER>/locations/global/workloadIdentityPools/<POOL_NAME>/providers/<PROVIDER_NAME>`.

### Grant the AWS role access to the bucket

```sh
gcloud storage buckets add-iam-policy-binding gs://<GCS_BUCKET> \
  --member="principalSet://iam.googleapis.com/projects/<GCP_PROJECT_NUMBER>/locations/global/workloadIdentityPools/<POOL_NAME>/attribute.aws_role/<AWS_ROLE_NAME>" \
  --role="roles/storage.objectAdmin"

gcloud storage buckets add-iam-policy-binding gs://<GCS_BUCKET> \
  --member="principalSet://iam.googleapis.com/projects/<GCP_PROJECT_NUMBER>/locations/global/workloadIdentityPools/<POOL_NAME>/attribute.aws_role/<AWS_ROLE_NAME>" \
  --role="roles/storage.bucketViewer"
```

Repeat both bindings for every database node role, and for the IAM role attached to the VM running YugabyteDB Anywhere if YugabyteDB Anywhere is running on AWS.

## Prepare Amazon S3 for GCP nodes

Do this in the AWS account that owns the bucket. The GCP identity is the service account attached to each database VM. The S3 actions go on the role you name in the provider. The service account stays attached to the VM and doesn't receive those S3 actions.

If YugabyteDB Anywhere is also running on GCP, add the numeric ID of its service account to the `accounts.google.com:sub` array in the trust policy.

### Get the service account numeric ID

```sh
gcloud iam service-accounts describe <SA_EMAIL> \
  --project=<GCP_PROJECT> --format="value(uniqueId)"
```

### Choose an audience

Use any URI you control. Enter the same value in **Audience** on the provider.

```text
https://example.com/yb-s3-federation
```

### Create a role that trusts the service account

Save the trust policy as `trust.json`. Condition on `accounts.google.com:sub`.

AWS maps the token's `azp` claim to `accounts.google.com:aud`, and the audience URL to `accounts.google.com:oaud`. A trust policy that matches `aud` to your audience URL fails with `AccessDenied`.

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "Federated": "accounts.google.com"
      },
      "Action": "sts:AssumeRoleWithWebIdentity",
      "Condition": {
        "StringEquals": {
          "accounts.google.com:sub": [
            "<GCP_SA_NUMERIC_ID>",
            "<SECOND_GCP_SA_NUMERIC_ID>"
          ]
        }
      }
    }
  ]
}
```

Put every service account numeric ID in that `accounts.google.com:sub` array. Then create the role:

```sh
aws iam create-role --role-name <ROLE_NAME> \
  --assume-role-policy-document file://trust.json \
  --query Role.Arn --output text
```

The command prints the role ARN, `arn:aws:iam::<account-id>:role/<ROLE_NAME>`. That ARN is what you enter on the provider.

Grant that role permission to the one bucket. In the AWS console, open **IAM > Roles**, select the role, and choose **Add permissions > Create inline policy > JSON**. This policy is on the role, not on the EC2 instance. It does not include `s3:ListAllMyBuckets`. That action lists every bucket in the account, and the role does not need it because the backup already names this bucket. The policy does include `s3:ListBucketMultipartUploads`, `s3:AbortMultipartUpload`, and `s3:ListMultipartUploadParts`, because the backup upload is a multipart upload. The [same-cloud S3 permission list](../../prepare/cloud-permissions/cloud-permissions-storage/#aws) includes `s3:ListAllMyBuckets` and does not include those multipart actions.

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "s3:ListBucket",
        "s3:GetBucketLocation",
        "s3:ListBucketMultipartUploads"
      ],
      "Resource": "arn:aws:s3:::<S3_BUCKET>"
    },
    {
      "Effect": "Allow",
      "Action": [
        "s3:PutObject",
        "s3:GetObject",
        "s3:DeleteObject",
        "s3:AbortMultipartUpload",
        "s3:ListMultipartUploadParts"
      ],
      "Resource": "arn:aws:s3:::<S3_BUCKET>/*"
    }
  ]
}
```

To attach the policy from the CLI, save the JSON above as `policy.json` and run:

```sh
aws iam put-role-policy --role-name <ROLE_NAME> \
  --policy-name yb-s3-backup \
  --policy-document file://policy.json
```

### Create the bucket

For a region other than `us-east-1`:

```sh
aws s3api create-bucket --bucket <S3_BUCKET> --region <REGION> \
  --create-bucket-configuration LocationConstraint=<REGION>
```

For `us-east-1`, omit `--create-bucket-configuration`. AWS rejects `LocationConstraint` in that region.

## Allow outbound access

If your network limits outbound traffic, allow the token service as well as the storage service. A backup that can reach the bucket and not the token service fails with an error that looks like a credential problem.

| Direction | Allow |
| :--- | :--- |
| Google Cloud Storage from AWS nodes | `sts.googleapis.com`, `storage.googleapis.com`, and `169.254.169.254` |
| Amazon S3 from GCP nodes | `sts.amazonaws.com`, `s3.<region>.amazonaws.com`, and `metadata.google.internal` |

## Configure the provider

In YugabyteDB Anywhere, update your provider configuration. The provider shows only the clouds its nodes are not running on. You can change these settings while universes use the provider.

### AWS provider

To let nodes on this provider back up to Google Cloud Storage, do the following:

1. Navigate to **Integrations > Infrastructure** and edit the AWS provider.
1. Under **Access to other clouds** and **Services**, turn on **Google Cloud Storage**.
1. Upload the credential configuration file you downloaded from Google Cloud. The browser reads the audience from the file. The file is not sent to YugabyteDB Anywhere.

### GCP provider

To let nodes on this provider back up to Amazon S3, do the following:

1. Navigate to **Integrations > Infrastructure** and edit the GCP provider.
1. Under **Access to other clouds** and **Services**, turn on **Amazon S3**.
1. In **IAM role ARN**, enter the role ARN printed by [Create a role that trusts the service account](#create-a-role-that-trusts-the-service-account).
1. In **Audience**, enter the URI from [Choose an audience](#choose-an-audience).

### On-premises provider

An on-premises provider configuration can include both clouds. Each AWS or Google Cloud node uses only the cloud it is not running on. A bare-metal node is skipped, and the universe task continues.

To configure an on-premises provider configuration, do the following:

1. Navigate to **Integrations > Infrastructure** and edit the on-premises provider.
1. Under **Cross Cloud Federated Access** and **Services**, turn on **Google Cloud Storage**, **Amazon S3**, or both.
1. Upload the credential configuration file for Google Cloud Storage. For Amazon S3, enter the **IAM role ARN** printed by [Create a role that trusts the service account](#create-a-role-that-trusts-the-service-account) and the **Audience** from [Choose an audience](#choose-an-audience).

| Node | Other cloud it backs up to | Same-cloud access |
| :--- | :--- | :--- |
| AWS VM | Google Cloud Storage, using its IAM role | The S3 storage configuration |
| GCP VM | Amazon S3, using its service account | The Google Cloud Storage configuration |

Set up each cloud you turn on. Google Cloud Storage is used by the AWS nodes, and Amazon S3 is used by the GCP nodes.

## Configure backup storage

Create the storage configuration under **Integrations > Backup**. Leave the access key, secret, and GCS credentials empty.

**Federated IAM** controls how YugabyteDB Anywhere reaches the bucket. Turn it on if YugabyteDB Anywhere is running on a different cloud from the bucket. Leave it off if YugabyteDB Anywhere is running on the same cloud as the bucket. In that case YugabyteDB Anywhere uses the instance profile or service account of the instance where it is running.

### Identity the database nodes use

With **IAM Role**, YugabyteDB Anywhere checks the bucket when you save. With **Federated IAM**, that check is skipped, and YugabyteDB Anywhere uses federation only to delete backups and validate the configuration. A delete still works after the universe is gone. Existing storage configurations are unchanged.

| YugabyteDB Anywhere runs on | Bucket | Setting | Identity the database nodes use |
| :--- | :--- | :--- | :--- |
| AWS | S3 | **IAM Role** on. **Federated IAM** off. When a database node is running on GCP, set **Use S3 IAM roles attached to DB node for Backup/Restore** (config key `yb.backup.s3.use_db_nodes_iam_role_for_backup`) to true. | A GCP node uses the credential process from the provider's **Amazon S3** setting. An AWS node uses its instance role when that option is true. When the option is false, the node uses temporary credentials from the instance profile of the instance where YugabyteDB Anywhere is running. |
| AWS | Google Cloud Storage | **Federated IAM** on. | The IAM role attached to each database VM. |
| GCP | S3 | **Federated IAM** on. | The service account attached to each database VM. |
| GCP | Google Cloud Storage | **Use GCP IAM** on. **Federated IAM** off. | The service account attached to each database VM. |

An S3 configuration with an access key and secret, and a Google Cloud Storage configuration with **GCS Credentials**, send those credentials to the nodes.

Set **Use S3 IAM roles attached to DB node for Backup/Restore** (config key `yb.backup.s3.use_db_nodes_iam_role_for_backup`) to true when a database node is running on GCP, the bucket is in S3, and YugabyteDB Anywhere is running on AWS. The option defaults to false. Left false, that node receives temporary credentials from YugabyteDB Anywhere instead of using its service account, and turning **Enable Federated Access for Backups** off does not stop the backup. When YugabyteDB Anywhere is running on GCP, **Federated IAM** already selects the node's service account, so leave the option at its default. Refer to [Create an AWS backup configuration](../configure-backup-storage/#create-an-aws-backup-configuration).

When **Federated IAM** is on, YugabyteDB Anywhere needs its own permission. The role or service account on the database nodes does not authorize YugabyteDB Anywhere. If YugabyteDB Anywhere is running on AWS, add the IAM role attached to that instance to the [Google Cloud Storage bucket binding](#grant-the-aws-role-access-to-the-bucket). If YugabyteDB Anywhere is running on GCP, add the numeric ID of the service account attached to that instance to the [Amazon S3 role trust policy](#create-a-role-that-trusts-the-service-account).

### Google Cloud Storage

To create a Google Cloud Storage configuration, do the following:

1. Navigate to **Integrations > Backup > Google Cloud Storage**.
1. Click **Create GCS Backup**.
1. Enter the bucket URI, for example `gs://<GCS_BUCKET>/backups`.
1. Leave **GCS Credentials** empty.
1. Set **Use GCP IAM** and **Federated IAM** from the table above. Turn on **Use GCP IAM** if YugabyteDB Anywhere is running on GCP. Turn on **Federated IAM** if YugabyteDB Anywhere is running on AWS.
1. Click **Save**.

### Amazon S3

To create an Amazon S3 configuration, do the following. Set **S3 Bucket Host Base** to the bucket's region. A blank value defaults to `us-east-1`, and the backup fails with invalid credentials.

1. Navigate to **Integrations > Backup > Amazon S3**.
1. Click **Create S3 Backup**.
1. Enter the bucket, for example `s3://<S3_BUCKET>`.
1. In **S3 Bucket Host Base**, enter the bucket's regional endpoint, for example `s3.us-west-2.amazonaws.com`.

    If you leave the field empty, the region defaults to `us-east-1`, and backups will fail due to invalid credentials.

1. Leave **Access Key** and **Access Secret** empty.
1. Set **IAM Role** and **Federated IAM** from the table above. Turn on **IAM Role** if YugabyteDB Anywhere is running on AWS. Turn on **Federated IAM** if YugabyteDB Anywhere is running on GCP. When YugabyteDB Anywhere is running on AWS and a database node is running on GCP, set **Use S3 IAM roles attached to DB node for Backup/Restore** (config key `yb.backup.s3.use_db_nodes_iam_role_for_backup`) to true.
1. Click **Save**.

## Create or update the universe

Attach an IAM role or GCP service account to every database VM that backs up to the other cloud. Those nodes use that identity for the other cloud.

YugabyteDB Anywhere applies the provider settings when you create a universe on a provider that has **Google Cloud Storage** or **Amazon S3** turned on, and when you edit the universe, add a node, or replace a node.

To turn this on for a universe that already exists:

1. Open the universe and select **Actions > Edit Cross-Cloud Federated Access**.
1. Turn on **Enable Federated Access for Backups**.
1. Click **Apply Changes**.

The provider must already have **Google Cloud Storage** or **Amazon S3** turned on. Turning **Enable Federated Access for Backups** off stops backups and restores that use the other cloud, including scheduled backups. For a GCP node and an S3 bucket, that is true when **Use S3 IAM roles attached to DB node for Backup/Restore** is true, or when **Federated IAM** is on. With **IAM Role** on, **Federated IAM** off, and that option false, the node keeps using temporary credentials from YugabyteDB Anywhere. Turning the universe setting off does not remove the permissions you created in AWS or Google Cloud.

## Back up and verify

1. On the universe, open the **Backups** tab and select **Backup now**, using the storage configuration you created.
1. Confirm the backup completes. A failure that looks like a credential error can come from blocked token-service access; refer to [Allow outbound access](#allow-outbound-access).

Restores from a federated storage configuration use the same provider and universe settings. Turning off **Enable Federated Access for Backups** stops restores as well as backups.

