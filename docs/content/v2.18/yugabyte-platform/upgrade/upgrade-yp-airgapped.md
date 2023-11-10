---
title: Upgrade air-gapped YugabyteDB Anywhere installation
headerTitle: Upgrade air-gapped YugabyteDB Anywhere installation
linkTitle: Upgrade air-gapped installation
description: Upgrade air-gapped YugabyteDB Anywhere installation
menu:
  v2.18_yugabyte-platform:
    identifier: upgrade-yp-airgapped
    parent: upgrade
    weight: 81
type: docs
---

You can upgrade your air-gapped installation of YugabyteDB Anywhere to a newer version as follows:

1. Manually obtain and move the binary Replicated license file `<filename>.rli` to the `/home/{username}/`  directory.

2. Manually obtain and move the YugabyteDB Anywhere air-gapped package to the `/opt/yugabyte/releases/<new_version_dir>` directory.

   For example, if you are upgrading to the latest YugabyteDB Anywhere stable version, you would start by executing the following command to obtain the package:

   ```sh
   wget https://downloads.yugabyte.com/releases/{{<yb-version version="stable">}}/yugaware-{{<yb-version version="stable" format="build">}}-linux-x86_64.airgap
   ```

   Then you would create the `/opt/yugabyte/releases/yugaware-{{<yb-version version="stable" format="build">}}/` directory and move (or SCP) the `yugaware-{{<yb-version version="stable" format="build">}}-linux-x86_64.airgap` file into that directory.

3. Log in to the Replicated Admin Console at <https://:8800/> and navigate to **Settings** to load the new license file, as per the following illustration:

   ![Airgap Settings](/images/yp/airgap-settings.png)

   Change the two directories to match the ones you used. For example, enter `/opt/yugabyte/releases/yugaware-{{<yb-version version="stable" format="build">}}/` in the **Update Path** field and `/home/{user}/` in the **License File** field.

   Replicated detects updates based on the updated path information and applies them in the same way it does for connected YugabyteDB Anywhere installations.

4. Proceed with the YugabyteDB Anywhere upgrade process by following instructions provided in [Upgrade YugabyteDB Anywhere using Replicated](../upgrade-yp-replicated/).

5. Upgrade your YugabyteDB universe by following instructions provided in [Upgrade the YugabyteDB software](../../manage-deployments/upgrade-software/).
