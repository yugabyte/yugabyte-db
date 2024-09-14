---
title: Cloud setup for backup and restore using YugabyteDB Anywhere
headerTitle: To back up and restore
linkTitle: To back up and restore
description: Prepare your cloud for backup and restore using YugabyteDB Anywhere.
headContent: Prepare your cloud for backup and restore using YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: cloud-permissions-storage
    parent: cloud-permissions
    weight: 30
type: docs
---

When backing up to and/or restoring from external cloud storage, generally speaking, both YugabyteDB Anywhere (YBA) and database nodes require permissions to write to and read from the external storage.

When backing up to an NFS storage target, only database nodes need access to the NFS storage.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#onprem" class="nav-link active" id="onprem-tab" data-bs-toggle="tab"
      role="tab" aria-controls="onprem" aria-selected="true">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="#aws" class="nav-link" id="aws-tab" data-bs-toggle="tab"
      role="tab" aria-controls="aws" aria-selected="false">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#gcp" class="nav-link" id="gcp-tab" data-bs-toggle="tab"
      role="tab" aria-controls="gcp" aria-selected="false">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="#azure" class="nav-link" id="azure-tab" data-bs-toggle="tab"
      role="tab" aria-controls="azure" aria-selected="false">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="onprem" class="tab-pane fade show active" role="tabpanel" aria-labelledby="onprem-tab">

When backing up to and/or restoring from NFS storage, the NFS storage system must be configured to allow the following access:

- The `yugabyte` user (and its UID) on the database cluster nodes needs to have read and write permissions for the NFS volume.
- The NFS volume must be mounted on the database cluster nodes.

(This guidance is intentionally repeated in [Prepare Servers for On-Premises provider](../../server-nodes-software/software-on-prem-manual/), where it may be more suitable for some readers.)

  </div>

  <div id="aws" class="tab-pane fade" role="tabpanel" aria-labelledby="aws-tab">

When backing up to and/or restoring from AWS S3, YBA and DB nodes must be able to write to and read from the S3 storage bucket.

To grant the required access, you can do one of the following:

- Provide a service account with the following permissions.
- Create the EC2 VM instances (for both the YBA VM and the DB nodes VMs) with an IAM role with the required permissions.

The following permissions are required:

```sh
"s3:DeleteObject",
"s3:PutObject",
"s3:GetObject",
"s3:ListBucket",
"s3:GetBucketLocation"
```

The Access key ID and Secret Access Key for the service account are used when creating a backup [storage configuration](../../../back-up-restore-universes/configure-backup-storage/#amazon-s3) for S3.

| Save for later | To configure |
| :--- | :--- |
| Service account Access key ID and Secret Access Key | [Storage configuration](../../../back-up-restore-universes/configure-backup-storage/#amazon-s3) for S3 |

  </div>

  <div id="gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="gcp-tab">

When backing up to and/or restoring from GCP GCS, YBA and database nodes must be able to write to and read from the GCS storage bucket.

To grant the required access, create a GCP service account with [IAM roles for cloud storage](https://cloud.google.com/storage/docs/access-control/iam-roles) with the following permissions:

```sh
roles/storage.admin
```

The credentials for this account (in JSON format) are used when creating a backup [storage configuration](../../../back-up-restore-universes/configure-backup-storage/#google-cloud-storage) for GCS.

| Save for later | To configure |
| :--- | :--- |
| Storage service account JSON credentials | [Storage configuration](../../../back-up-restore-universes/configure-backup-storage/#google-cloud-storage) for GCS |

For database clusters deployed to GKE, you can alternatively assign the appropriate IAM roles to the YugabyteDB Anywhere VM and the YugabyteDB nodes.

  </div>

  <div id="azure" class="tab-pane fade" role="tabpanel" aria-labelledby="azure-tab">

When backing up to and/or restoring from Azure Storage, YBA and DB nodes must be able to write to and read from the storage blob.

To grant the required access, create a [Shared Access Signature (SAS)](https://learn.microsoft.com/en-us/azure/storage/common/storage-sas-overview) token with the permissions as shown in the following illustration.

![Azure Shared Access Signature page](/images/yp/cloud-provider-configuration-backup-azure-generate-token.png)

The Connection string and SAS token are used when creating a backup [storage configuration](../../../back-up-restore-universes/configure-backup-storage/#azure-storage) for Azure.

| Save for later | To configure |
| :--- | :--- |
| Azure storage Connection string and SAS token | [Storage configuration](../../../back-up-restore-universes/configure-backup-storage/#azure-storage) for Azure |

  </div>

</div>
