---
title: Configure backup storage
headerTitle: Configure backup storage
linkTitle: Configure backup storage
description: Configure backup storage
headContent: Store your backups in the cloud or on NFS
aliases:
  - /preview/yugabyte-platform/configure-yugabyte-platform/backup-target/
menu:
  preview_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: configure-backup-storage
    weight: 10
type: docs
---

Before you can back up universes, you need to configure a storage location for your backups.

Depending on your environment, you can save your YugabyteDB universe data to a variety of storage solutions.

## Local storage

If your YugabyteDB universe has one node, you can create a local directory on a T-Server to which to back up, as follows:

1. Navigate to **Universes**, select your universe, and then select **Nodes**.

2. Click **Connect**.

3. Take note of the services and endpoints information displayed in the **Connect** dialog, as shown in the following illustration:

    ![Connect dialog](/images/yp/cloud-provider-local-backup1.png)

4. While connected using `ssh`, create a directory `/backup` and then change the owner to `yugabyte`, as follows:

    ```sh
    sudo mkdir /backup; sudo chown yugabyte /backup
    ```

If there is more than one node, you should consider using a network file system mounted on each server.

## Amazon S3

You can configure Amazon S3 as your backup target, as follows:

1. Navigate to **Integrations** > **Backup** > **Amazon S3**.

2. Click **Create S3 Backup** to access the configuration form shown in the following illustration:

    ![S3 Backup](/images/yp/cloud-provider-configuration-backup-aws.png)

3. Use the **Configuration Name** field to provide a meaningful name for your backup configuration.

4. Enable **IAM Role** to use the YugabyteDB Anywhere instance's Identity Access Management (IAM) role for the S3 backup. See [Required S3 IAM permissions](#required-s3-iam-permissions).

5. If **IAM Role** is disabled, enter values for the **Access Key** and **Access Secret** fields.

6. Enter values for the **S3 Bucket** and **S3 Bucket Host Base** fields.

    For information on how to obtain AWS credentials, see [Understanding and getting your AWS credentials](https://docs.aws.amazon.com/general/latest/gr/aws-sec-cred-types.html).

7. Click **Save**.

You can configure access control for the S3 bucket as follows:

- Provide the required access control list (ACL), and then define **List, Write** permissions to access **Objects**, as well as **Read, Write** permissions for the bucket, as shown in the following illustration:

    ![S3](/images/yp/backup-aws-access-control.png)

- Create Bucket policy to enable access to the objects stored in the bucket.

### Required S3 IAM permissions

The following S3 IAM permissions are required:

```properties
"s3:DeleteObject",
"s3:PutObject",
"s3:GetObject",
"s3:ListBucket",
"s3:GetBucketLocation"
```

## Network File System

You can configure Network File System (NFS) as your backup target, as follows:

1. Navigate to **Integrations > Backup > Network File System**.

2. Click **Create NFS Backup** to access the configuration form shown in the following illustration:

    ![NFS Configuration](/images/yp/cloud-provider-configuration-backup-nfs.png)

3. Use the **Configuration Name** field to provide a meaningful name for your backup configuration.

4. Complete the **NFS Storage Path** field by entering `/backup` or another directory that provides read, write, and access permissions to the SSH user of the YugabyteDB Anywhere instance.

5. Click **Save**.

## Google Cloud Storage

You can configure Google Cloud Storage (GCS) as your backup target, as follows:

1. Navigate to **Integrations > Backup > Google Cloud Storage**.

1. Click **Create GCS Backup** to access the configuration form shown in the following illustration:

    ![GCS Configuration](/images/yp/cloud-provider-configuration-backup-gcs-stable.png)

1. Use the **Configuration Name** field to provide a meaningful name for your backup configuration.

1. Enter the URI of your GCS bucket in the **GCS Bucket** field. For example, `gs://gcp-bucket/test_backups`.

1. Select **Use GCP IAM** if you're using [GKE service account](#gke-service-account-based-iam-gcp-iam) for backup and restore.

1. Complete the **GCS Bucket** and **GCS Credentials** fields.

    For information on how to obtain GCS credentials, see [Cloud Storage authentication](https://cloud.google.com/storage/docs/authentication).

1. Click **Save**.

You can configure access control for the GCS bucket as follows:

- Provide the required access control list (ACL) and set it as either uniform or fine-grained (for object-level access).
- Add permissions, such as roles and members.

### GKE service account-based IAM (GCP IAM)

Google Kubernetes Engine (GKE) uses a concept known as "Workload Identity" to provide a secure way to allow a Kubernetes service account (KSA) in your GKE cluster to act as an IAM service account so that your Kubernetes universes can access GCS for backups.

In GKE, each pod can be associated with a KSA. The KSA is used to authenticate and authorize the pod to interact with other Google Cloud services. An IAM service account is a Google Cloud resource that allows applications to make authorized calls to Google Cloud APIs.

Workload Identity links a KSA to an IAM account using annotations in the KSA. Pods that use the configured KSA automatically authenticate as the IAM service account when accessing Google Cloud APIs.

By using Workload Identity, you avoid the need for manually managing service account keys or tokens in your applications running on GKE. This approach enhances security and simplifies the management of credentials.

#### Prerequisites

- The GKE cluster hosting the pods should have Workload Identity enabled. The worker nodes of this GKE cluster should have the GKE metadata server enabled.

- The IAM service account, which is used to annotate the KSA, should have sufficient permissions to read, write, list, and delete objects in GCS.

- The KSA, which is annotated with the IAM service account, should be present in the same namespace where the pod resources for YugabyteDB Anywhere and YugabyteDB universes are expected. If you have multiple namespaces, each namespace should include the annotated KSA.

For instructions on setting up Workload Identity, see [Use Workload Identity](https://cloud.google.com/kubernetes-engine/docs/how-to/workload-identity) in the GKE documentation.

To enable GCP IAM when installing YugabyteDB Anywhere, refer to [Enable GKE service account-based IAM](../../install-yugabyte-platform/install-software/kubernetes/#enable-gke-service-account-based-iam).

To enable GCP IAM during universe creation, refer to [Configure Helm overrides](../../create-deployments/create-universe-multi-zone-kubernetes/#configure-helm-overrides).

To upgrade an existing universe with GCP IAM, refer to [Upgrade universes for GKE service account-based IAM support](../../manage-deployments/edit-helm-overrides/#upgrade-universes-for-gke-service-account-based-iam).

## Azure Storage

You can configure Azure as your backup target, as follows:

1. Create a storage account in Azure, as follows:

    - Navigate to **Portal > Storage Account** and click **Add** (+).
    - Complete the mandatory fields, such as **Resource group**, **Storage account name**, and **Location**, as per the following illustration:

        ![Azure storage account creation](/images/yp/cloud-provider-configuration-backup-azure-account.png)

1. Create a blob container, as follows:

    - Open the storage account (for example, **storagetestazure**, as shown in the following illustration).
    - Navigate to **Blob service > Containers > + Container** and then click **Create**.

        ![Azure blob container creation](/images/yp/cloud-provider-configuration-backup-azure-blob-container.png)

1. Obtain the container URL by navigating to **Container > Properties**, as shown in the following illustration:

    ![Azure container properties](/images/yp/cloud-provider-configuration-backup-azure-container-properties.png)

1. Generate an SAS Token, as follows:

    - Navigate to **Storage account > Shared access signature**, as shown in the following illustration. (NOTE: the SAS Token must be generated on the Storage Account, not the Container. Generating the SAS Token on the container will prevent the configuration from being applied.)
    - Under **Allowed resource types**, select **Container** and **Object**.
    - Click **Generate SAS and connection string** and copy the SAS token. Note that the token should start with `?sv=`.

        ![Azure Shared Access Signature page](/images/yp/cloud-provider-configuration-backup-azure-generate-token.png)

1. On your YugabyteDB Anywhere instance, provide the container URL and SAS token for creating a backup, as follows:

    - Navigate to **Integrations** > **Backup** > **Azure Storage**.
    - Click **Create AZ Backup** to access the configuration form shown in the following illustration:

        ![Azure Configuration](/images/yp/cloud-provider-configuration-backup-azure.png)

    - Use the **Configuration Name** field to provide a meaningful name for your backup configuration.
    - Enter values for the **Container URL** and **SAS Token** fields, and then click **Save**.
