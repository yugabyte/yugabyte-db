---
title: Manage YugabyteDB releases available in YugabyteDB Anywhere
headerTitle: Manage YugabyteDB releases
linkTitle: Manage releases
description: View and import releases of YugabyteDB into YugabyteDB Anywhere.
headcontent: View and import database releases for use in universes
menu:
  stable_yugabyte-platform:
    identifier: ybdb-releases
    parent: upgrade-software
    weight: 15
type: docs
---

Before you can upgrade your universe to a specific version of YugabyteDB, verify that the release is available in YugabyteDB Anywhere and, if necessary, import it.

## View YugabyteDB releases

To view the YugabyteDB releases that are available in your YugabyteDB Anywhere instance, click the user profile icon and choose **Releases**.

![Releases](/images/yp/releases-list-2024.png)

You can filter the list by text and release type, and choose to display only allowed releases.

To view the release details, select a release in the list.

To delete or disable a release, click its corresponding **Actions**. Disabled releases are not available when creating universes.

## Import a YugabyteDB release

If the YugabyteDB release you want to use for your universe is not in the **Releases** list, you must import it to your YugabyteDB Anywhere instance.

If you are running YugabyteDB Anywhere airgapped, download the YugabyteDB release to a machine that is accessible to your instance so that you can upload the file.

If your YugabyteDB Anywhere instance has internet access, you can import the release from a bucket or an internal server using a URL.

You can obtain YugabyteDB releases by navigating to the release from the [YugabyteDB releases](../../../releases/ybdb-releases/) page.

{{< note title="Importing Stable and Preview versions" >}}
By default, you cannot import the latest stable YugabyteDB versions (v2024.1.x) in preview YugabyteDB Anywhere versions (for example, v2.23.x).

To enable this import, you need to set the YugabyteDB Anywhere runtime flag `yb.allow_db_version_more_than_yba_version` to true. See [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/).
{{< /note >}}

To import a YugabyteDB release, do the following:

1. Navigate to **Releases** and click **New Release** to open the **Import Database Release** dialog.

    ![Import Release](/images/yp/import-releases-2024.png)

1. Choose whether to upload the release as a file, or download the release via a URL.

    - If uploading a file, click **Upload** and select the release file. Upload the package in tar.gz format. For example:

        `yugabyte-{{< yb-version version="stable" format="build">}}-linux-x86_64.tar.gz`

    - If loading the release from a URL, enter the URL of the storage location.

        Note that the package is not downloaded and cached locally for future universe creation. The URL must remain available, and every software installation on a node (for example, for scale out of a universe) downloads the release from this location.

1. Click **Fetch Metadata**. YugabyteDB Anywhere obtains the release details from the file.

    If YugabyteDB Anywhere is unable to extract the release details, provide them manually by entering the release version and specifying the deployment type and architecture.

1. Click **Add Release**.

When imported, the release is added to the **Releases** list.

### Add an architecture for a release

If a the architecture for a YugabyteDB release that you want to install on a universe is not available (for example, you have x86 but require ARM), add it as follows:

1. Navigate to **Releases**, locate the YugabyteDB release in the list, and click the plus button in the **Imported Architecture** column; or click **Actions** and **Add Architecture** to open the **New Architecture** dialog.

1. Choose whether to upload the release as a file, or download the release via a URL.

    - If uploading a file, click **Upload** and select the release file. Upload the package in tar.gz format. For example:

        `yugabyte-{{< yb-version version="stable" format="build">}}-el8-aarch64.tar.gz`

    - If loading the release from a URL, enter the URL of the storage location.

        Note that the package is not downloaded and cached locally for future universe creation. The URL must remain available, and every software installation on a node (for example, for scale out of a universe) downloads the release from this location.

1. Click **Fetch Metadata**. YugabyteDB Anywhere obtains the release details from the file.

    If YugabyteDB Anywhere is unable to extract the release details, provide them manually by entering the release version and specifying the deployment type and architecture.

1. Click **New Architecture**.

When imported, the architecture is added to the Release.

### Kubernetes

For Kubernetes universes, if a new Kubernetes release architecture is required, do the following:

1. [Create the release](#import-a-yugabytedb-release) using the related YugabyteDB x86/ARM build.
1. Add a new architecture to the newly created release and provide the helm chart.

As metadata is pulled from the YugabyteDB build, this will populate all the required data fields when creating the release, and simplify adding the helm chart after.
