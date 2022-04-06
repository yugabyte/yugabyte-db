---
title: Use Yugabyte Platform to edit a universe
headerTitle: Edit a universe
linkTitle: Edit a universe
description: Use Yugabyte Platform to edit a universe.
aliases:
  - /latest/manage/enterprise-edition/edit-universe/
menu:
  latest:
    identifier: edit-universe
    parent: manage-deployments
    weight: 60
isTocNested: true
showAsideToc: true
---

Yugabyte Platform allows you to modify an existing universe: you can expand it to add more nodes and shrink it to reduce the number of nodes; you can configure the Yugabyte Platform instance by changing its type; you can add user tags.

Typically, you modify the universe by navigating to **Universes**, selecting your universe, and then clicking **Actions > Edit Universe**, as per the following illustration:

![Edit universe](/images/ee/edit-univ.png)<br><br>

Using the **Edit Universe** page, you can specify the new intent for the universe. This may include a new configuration of nodes powered by a different instance type. 

Yugabyte Platform applies changes via the YB-Masters powering the universe. The YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

Expansion of universes created with an on-premise cloud provider and secured with third-party certificates obtained from external certification authorities follows a different workflow. For details, see [How to Expand the Universe](../../security/enable-encryption-in-transit#how-to-expand-the-universe).



For universes that use Google Cloud Provider (GCP) or Amazon Web Services (AWS), Yugabyte Platform allows you to change the virtual machine image and increasing the volume size without moving the data from the old nodes to the new nodes. This is known as smart resize and it is subject to the following:

- Smart resize cannot be applied to instances with ephemeral disks due to a potential loss of data, but smart resize to ephemeral disks is supported.

- Smart resize cannot decrease the volume size.

- The smart resize option is not presented if anything except the values of the **Instance Type** and the size portion of the **Volume Info** fields has been changed on the **Edit Universe** page.

- If you modify the value in the **Instance Type** field or in both the **Instance Type** and the size portion of the **Volume Info** field, and then click **Save**, you will be able to choose either the migration of the universe along with its data to new nodes or smart resize, as per the following illustrations:<br><br>

  ![Full or smart resize](/images/ee/edit-univ-2.png)<br><br>

- If you only modify the value in the size portion of the **Volume Info** field and click **Save**, smart resize will be performed.






