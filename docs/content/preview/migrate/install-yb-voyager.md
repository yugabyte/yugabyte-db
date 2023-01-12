---
title: Install
headerTitle: Install
linkTitle: Install
description: Explore the prerequisites, YugabyteDB Voyager installation, and so on.
image: /images/section_icons/develop/learn.png
menu:
  preview:
    identifier: install-yb-voyager
    parent: voyager
    weight: 101
type: docs
---

## Prerequisites

The following sections describe the prerequisites for installing YugabyteDB Voyager.

### Operating system

You can install YugabyteDB Voyager on the following:

- RHEL 7/8
- CentOS 7/8
- Ubuntu 18.04, 20.04, 22.04
- MacOS (currently supported only for PostgreSQL source database)

    {{< note title = "note" >}}

You will get the following error if you use MySQL/Oracle as the source database on MacOS:

```output
"ora2pg" not found. Check if it is installed and included in the path.
```

    {{< /note >}}

### Hardware requirements

- Disk space must be at least 1.5 times the estimated size of the source database.
- 2 cores minimum (recommended)

### Prepare the host

The node where you'll run the yb-voyager command should:

- connect to both the source and the target database.
- have sudo access.

### Create an export directory

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire data dump. Next, you should provide the path of the export directory as a mandatory argument (`--export-dir`) to each invocation of the yb-voyager command.

yb-voyager uses the directory to store source data, schema files, and the migration state. The file system in which the directory resides must have enough free space to hold the entire source database. Create an export directory in the local file system on the machine where YugabyteDB Voyager will be installed, and place its path in an environment variable.

```sh
mkdir $HOME/export-dir
export EXPORT_DIR=$HOME/export-dir
```

The export directory has the following sub-directories and files:

- `reports` directory contains the generated *Schema Analysis Report*.
- `schema` directory contains the source database schema translated to PostgreSQL. The schema is partitioned into smaller files by the schema object type such as tables, views, and so on.
- `data` directory contains TSV (Tab Separated Values) files that are passed to the COPY command on the target database.
- `metainfo` and `temp` directories are used by yb-voyager for internal bookkeeping.
- `yb-voyager.log` contains log messages.

<!-- For more information, refer to [Export directory](../../yb-voyager/reference/#export-directory). -->

## Install yb-voyager

YugabyteDB Voyager consists of the yb-voyager command line executable. yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the [*export directory*](#create-an-export-directory).

Install yb-voyager on a machine which satisfies the [Prerequisites](#prerequisites) using one of the following options:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#linux" class="nav-link active" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="true">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      RHEL
    </a>
  </li>
  <li>
    <a href="#ubuntu" class="nav-link" id="ubuntu-tab" data-toggle="tab" role="tab" aria-controls="ubuntu" aria-selected="true">
      <i class="fa-brands fa-ubuntu" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>
    <li >
    <a href="#macos" class="nav-link" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#airgapped" class="nav-link" id="airgapped-tab" data-toggle="tab" role="tab" aria-controls="airgapped" aria-selected="true">
      <i class="fa-solid fa-link-slash" aria-hidden="true"></i>
      Airgapped
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="true">
      <i class="fa-brands fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#github" class="nav-link" id="github-tab" data-toggle="tab" role="tab" aria-controls="github" aria-selected="true">
      <i class="fab fa-github" aria-hidden="true"></i>
      Source
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="linux" class="tab-pane fade show active" role="tabpanel" aria-labelledby="linux-tab">
{{% readfile "./linux.md" %}}
  </div>
  <div id="ubuntu" class="tab-pane fade" role="tabpanel" aria-labelledby="ubuntu-tab">
{{% readfile "./ubuntu.md" %}}
  </div>
  <div id="macos" class="tab-pane fade" role="tabpanel" aria-labelledby="macos-tab">
{{% readfile "./macos.md" %}}
  </div>
  <div id="airgapped" class="tab-pane fade" role="tabpanel" aria-labelledby="airgapped-tab">
{{% readfile "./airgapped.md" %}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
{{% readfile "./docker.md" %}}
  </div>
  <div id="github" class="tab-pane fade" role="tabpanel" aria-labelledby="github-tab">
{{% readfile "./github.md" %}}
  </div>
</div>

To learn more about yb-voyager, refer to [YugabyteDB Voyager CLI](../yb-voyager-cli/).

## Next step

- [Migration steps](../migrate-steps/)
