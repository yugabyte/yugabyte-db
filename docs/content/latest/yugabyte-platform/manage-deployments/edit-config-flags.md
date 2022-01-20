---
title: Edit configuration flags
headerTitle: Edit configuration flags
linkTitle: Edit configuration flags
description: Use Yugabyte Platform to edit configuration flags.
aliases:
  - /latest/manage/enterprise-edition/edit-flags/
  - /latest/manage/enterprise-edition/edit-config-flags/
menu:
  latest:
    identifier: edit-config-flags
    parent: manage-deployments
    weight: 50
isTocNested: true
showAsideToc: true
---

Modify the configuration flags for your YB-Master and YB-TServer nodes in a YugabyteDB universe allows you to resolve issues, improve performance, and customize functionality.

For more information about the available configuration flags, see the following:

- [YB-TServer configuration reference](../../../reference/configuration/yb-tserver/)
- [YB-Master configuration reference](../../../reference/configuration/yb-master/)

You can modify configuration flags by opening the universe in the Yugabyte Platform console and clicking **Actions > Edit Flags** to open the **Flags** dialog shown in the following illustration:<br><br>
![Edit Config Confirmation](/images/ee/edit-config-2.png)<br><br>

Use the **Flags** dialog to do all or some of the following, as per your requirements:

- Click **Add Flags > Add to Master** to open the **Add to Master** dialog, then select the flag you want to add to YB-Master and set its value, as per the following illustration:<br><br>
  
  ![Master Config](/images/ee/add-master-1.png)<br><br>

- Click **Add Flags > Add to T-Server** to open the **Add to T-Server** dialog, then select the flag you want to add to YB-TServer and set its value.
- Use **Add as Free text > Add to Master** and **Add as Free text > Add to T-Server** to add the flags via text.

- Use the **FLAG NAME** column to find the flag you want to modify and perform all or some of the following:
  
  - Click the **Edit Flag** icon for either **MASTER VALUE** or **T-SERVER VALUE** to open the **Add to Master** or **Add to T-Server** dialog, as per the following illustration, change the value in the **Flag Value** field and then click **Add Flag**:<br><br>
  
    ![Master Config](/images/ee/master-flag-1.png)<br><br>
  
  - To delete a flag, click the **Remove Flag** icon for either **MASTER VALUE** or **T-SERVER VALUE** or both.
  
  - Repeat the preceding procedures for every flag that needs modifications.
  
- Modify the flag upgrade type.

  

