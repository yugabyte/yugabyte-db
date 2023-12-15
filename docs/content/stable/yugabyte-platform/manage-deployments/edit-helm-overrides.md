---
title: Edit Kubernetes overrides
headerTitle: Edit Kubernetes overrides
linkTitle: Edit Kubernetes overrides
description: Edit Kubernetes overrides for a universe.
menu:
  stable_yugabyte-platform:
    identifier: edit-helm-overrides
    parent: manage-deployments
    weight: 60
type: docs
---

If your universe was created using Kubernetes, you have an option of modifying the Helm chart overrides. To do this, navigate to your universe's **Overview**, click **Actions > Edit Kubernetes Overrides** to open the **Kubernetes Overrides** dialog shown in the following illustration:

![img](/images/yb-platform/kubernetes-config66.png)

Complete the dialog by following instructions provided in [Configure Helm overrides](../../create-deployments/create-universe-multi-zone-kubernetes#configure-helm-overrides).

## Upgrade universes for GKE service account-based IAM

Upgrade existing universes to versions that support [GKE service account-based IAM](../../back-up-restore-universes/configure-backup-storage/#gke-service-account-based-iam-gcp-iam) using the following steps:

1. Upgrade YugabyteDB to a version which supports the feature (2.18.x or later). For more details, refer to [Upgrade the YugabyteDB software](../../manage-deployments/upgrade-software/).

1. Apply overrides by following the preceding steps in Edit Kubernetes overrides.

   **Prerequisite:** Provide the Kube namespace of the YugabyteDB universe pods with the annotated Kubernetes service account. This may not be present already, as the service account would not have been used in YugabyteDB Universe pods.

   Provide the following overrides after upgrading the YugabyteDB software:

   - serviceAccount: Pass the service account name created in [prerequisites](../../back-up-restore-universes/configure-backup-storage/#prerequisites). Note that this service account should be present in the namespace being used for the YugabyteDB pod resources.
   - [nodeSelector](../../install-yugabyte-platform/install-software/kubernetes/#nodeselector): Pass a node selector override to make sure that the YugabyteDB pods are scheduled on the GKE cluster's worker nodes that have a metadata server running.

    ```yaml
    tserver:
      serviceAccount:<KSA_NAME>
    nodeSelector:
      iam.gke.io/gke-metadata-server-enabled: "true"
    ```
