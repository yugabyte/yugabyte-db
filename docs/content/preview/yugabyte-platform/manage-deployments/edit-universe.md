---
title: Use YugabyteDB Anywhere to edit a universe
headerTitle: Edit a universe
linkTitle: Edit a universe
description: Use YugabyteDB Anywhere to edit a universe.
aliases:
  - /preview/manage/enterprise-edition/edit-universe/
menu:
  preview_yugabyte-platform:
    identifier: edit-universe
    parent: manage-deployments
    weight: 60
type: docs
---

YugabyteDB Anywhere allows you to expand a universe to add more nodes and shrink the universe to reduce the number of nodes. Typically, you do this by navigating to **Universes > UniverseName > Actions > Edit Universe**, as per the following illustration:

![Edit universe](/images/ee/edit-univ.png)

Optionally, use the **Helm Overrides** section as follows:

- Click **Add Kubernetes Overrides** to open the **Kubernetes Overrides** dialog shown in the following illustration:

  ![img](/images/yb-platform/kubernetes-config66.png)

- Using the YAML format, which is sensitive to spacing and indentation, specify the universe-level overrides for YB-Master and YB-TServer, as per the following example:

  ```yaml
  master:
    podLabels:
      service-type: 'database'
  ```

- Add availability zone overrides, which only apply to pods that are deployed in that specific availability zone. For example, to define overrides for the availability zone us-west-2a, you would click **Add Availability Zone** and use the text area to insert YAML in the following form:

  ```yaml
  us-west-2a:
    master:
      podLabels:
         service-type: 'database'
  ```

  If you specify conflicting overrides, YugabyteDB Anywhere would use the following order of precedence: universe availability zone-level overrides, universe-level overrides, provider overrides.

- Select **Force Apply** if you want to override any previous overrides.

- Click **Validate & Save**.

  If there are any errors in your overrides definitions, a detailed error message is displayed. You can correct the errors and try to save again. To save your Kubernetes overrides regardless of any validation errors, select **Force Apply**.





Using the **Edit Universe** page, you can specify the new intent for the universe. This may include a new configuration of nodes powered by a different instance type.

YugabyteDB Anywhere performs these modifications through the YB-Masters powering the universe. The YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

Expansion of universes created with an on-premise cloud provider and secured with third-party certificates obtained from external certification authorities follows a different workflow. For details, see [How to Expand the Universe](../../security/enable-encryption-in-transit#how-to-expand-the-universe).

