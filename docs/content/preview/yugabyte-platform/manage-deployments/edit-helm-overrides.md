---
title: Edit Kubernetes overrides
headerTitle: Edit Kubernetes overrides
linkTitle: Edit Kubernetes overrides
description: Edit Kubernetes overrides for a universe.
menu:
  preview_yugabyte-platform:
    identifier: edit-helm-overrides
    parent: edit-universe
    weight: 90
type: docs
---

If your universe was created using Kubernetes, you have an option of modifying the Helm chart overrides.

## Edit Kubernetes overrides

To edit Kubernetes overrides, do the following:

1. Navigate to your universe's **Overview**.

1. Click **Actions > Edit Kubernetes Overrides** to open the **Kubernetes Overrides** dialog shown in the following illustration:

    ![Kubernetes Overrides](/images/yb-platform/kubernetes-config66.png)

1. Complete the dialog by following instructions provided in [Configure Helm overrides](../../create-deployments/create-universe-multi-zone-kubernetes#configure-helm-overrides).

<!-- TODO Uncomment for 2.21
## Upgrade universes for GKE service account-based IAM

If you are using Google Cloud Storage (GCS) for backups, you can enable GKE service account-based IAM (GCP IAM) so that Kubernetes universes can access GCS.

Before upgrading a universe for GCP IAM, ensure you have the prerequisites. Refer to [GCP IAM](../../back-up-restore-universes/configure-backup-storage/#gke-service-account-based-iam-gcp-iam).

To upgrade an existing universe to use GCP IAM, do the following:

1. Upgrade YugabyteDB to a version that supports the feature (2.18.4 or later). For more details, refer to [Upgrade the YugabyteDB software](../../manage-deployments/upgrade-software/).

1. Using the steps in [Edit Kubernetes overrides](#edit-kubernetes-overrides), apply the following overrides.

    - serviceAccount: Provide the name of the Kubernetes service account you created. Note that this service account should be present in the namespace being used for the YugabyteDB pod resources.
    - [nodeSelector](../../install-yugabyte-platform/install-software/kubernetes/#nodeselector): Pass a node selector override to make sure that the YugabyteDB pods are scheduled on the GKE cluster's worker nodes that have a metadata server running.

    ```yaml
    tserver:
      serviceAccount: <KSA_NAME>
    nodeSelector:
      iam.gke.io/gke-metadata-server-enabled: "true"
    ```
-->
