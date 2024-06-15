---
title: Edit configuration flags
headerTitle: Edit configuration flags
linkTitle: Edit configuration flags
description: Use YugabyteDB Anywhere to edit configuration flags.
headcontent: Customize the database server configuration
menu:
  stable_yugabyte-platform:
    identifier: edit-config-flags
    parent: edit-universe
    weight: 10
type: docs
---

Adding and modifying configuration flags for your YB-Master and YB-TServer nodes in a YugabyteDB universe allows you to resolve issues, improve performance, and customize functionality. If your universe includes a read replica cluster, you can also add or modify configuration flags for the YB-TServer nodes in the read replica cluster.

For more information about the available configuration flags, see the following:

- [YB-TServer configuration reference](../../../reference/configuration/yb-tserver/)
- [YB-Master configuration reference](../../../reference/configuration/yb-master/)

## Modify configuration flags

You can add and edit configuration flags by opening the universe in the YugabyteDB Anywhere UI and clicking **Actions > Edit Flags** to open the **G-Flags** dialog shown in the following illustration:

![Modify configuration flags](/images/ee/edit-config-2.png)

To customize flags of the read replica of a universe that has a read replica cluster, deselect the **Apply the same Flags to primary cluster and Read Replica** option. (This option is only available for universes with a read replica.) This displays the **Read Replica** tab. [Add](#add-flags) and [modify](#edit-flags) flags as you would for the primary cluster. Note that read replicas only have YB-TServers.

Depending on the flag, the universe may need to be restarted to apply the changes. You can apply changes as follows:

- Immediately using a rolling restart.
- Immediately using a concurrent restart.
- Immediately apply any changes that do not require a restart and wait until the next time the universe is restarted to apply the remaining changes.

### Add flags

Click **Add Flags > Add to Master** to open the **Add to Master** dialog, then select the flag you want to add to YB-Master and set its value, as per the following illustration:

![Add flags to Master](/images/ee/add-master-1.png)

Click **Add Flags > Add to T-Server** to open the **Add to T-Server** dialog, then select the flag you want to add to YB-TServer and set its value.

Use **Add as JSON > Add to Master** and **Add as JSON > Add to T-Server** to import flags in bulk. The flags must be defined as key-value pairs in a JSON format via the in the **Add to T-Server** or **Add to Master** dialog, as per the following illustration:

![Add flags as JSON](/images/ee/add-gflags-json.png)

### Edit flags

To edit a flag:

1. Find the flag you want to change in the **FLAG NAME** column.

1. Click the **Edit Flag** icon for either **MASTER VALUE** or **T-SERVER VALUE** to open the **Edit Flag Value** dialog, as per the following illustration:

    ![Edit flag](/images/ee/master-flag-1.png)

1. Change the value in the **Flag Value** field and then click **Confirm**.

To delete the flag's value, click the **Remove Flag** icon for either **MASTER VALUE** or **T-SERVER VALUE** or both.

## Add configuration flags

You can add configuration flags when you are creating a new universe, as follows:

- Navigate to either **Dashboard** or **Universes** and click **Create Universe**.
- Complete the required sections of the **Create Universe** page.
- When you reach **G-Flags**, perform steps described in [Modify configuration flags](#modify-configuration-flags).
