---
title: Install yb-voyager
headerTitle: Install
linkTitle: Install
description: Prerequisites and installation instructions for YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: install-yb-voyager
    parent: yugabytedb-voyager
    weight: 101
type: docs
---

## Prerequisites

The following sections describe the prerequisites for installing YugabyteDB Voyager.

### Operating system

You can install YugabyteDB Voyager on the following:

- RHEL 8
- CentOS 8
- Ubuntu 18.04, 20.04, 22.04
- macOS (For MySQL/Oracle source databases on macOS, [install yb-voyager](#install-yb-voyager) using the Docker option.)

### Hardware requirements

- Disk space of at least 1.5 times the estimated size of the source database
- 2 cores minimum (recommended)

### Software requirement

- Java 17. Any higher versions of Java might lead to errors during installation or migration.

### Prepare the host

The node where you'll run the yb-voyager command should:

- connect to both the source and the target database.
- have sudo access.

## Install yb-voyager

YugabyteDB Voyager consists of the yb-voyager command line executable.

Install yb-voyager on a machine which satisfies the [Prerequisites](#prerequisites) using one of the following options:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#rhel" class="nav-link active" id="rhel-tab" data-bs-toggle="tab" role="tab" aria-controls="rhel" aria-selected="true">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      RHEL
    </a>
  </li>
  <li>
    <a href="#ubuntu" class="nav-link" id="ubuntu-tab" data-bs-toggle="tab" role="tab" aria-controls="ubuntu" aria-selected="true">
      <i class="fa-brands fa-ubuntu" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>
    <li >
    <a href="#macos" class="nav-link" id="macos-tab" data-bs-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#airgapped" class="nav-link" id="airgapped-tab" data-bs-toggle="tab" role="tab" aria-controls="airgapped" aria-selected="true">
      <i class="fa-solid fa-link-slash" aria-hidden="true"></i>
      Airgapped
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-bs-toggle="tab" role="tab" aria-controls="docker" aria-selected="true">
      <i class="fa-brands fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#github" class="nav-link" id="github-tab" data-bs-toggle="tab" role="tab" aria-controls="github" aria-selected="true">
      <i class="fab fa-github" aria-hidden="true"></i>
      Source
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="rhel" class="tab-pane fade show active" role="tabpanel" aria-labelledby="rhel-tab">
{{% readfile "./rhel.md" %}}
  </div>
  <div id="ubuntu" class="tab-pane fade" role="tabpanel" aria-labelledby="ubuntu-tab">
{{% readfile "./ubuntu.md" %}}
  </div>
  <div id="macos" class="tab-pane fade" role="tabpanel" aria-labelledby="macos-tab">
{{% readfile "./macos.md" %}}
  </div>
  <div id="airgapped" class="tab-pane fade" role="tabpanel" aria-labelledby="airgapped-tab">
{{< readfile "./airgapped.md" >}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
{{% readfile "./docker.md" %}}
  </div>
  <div id="github" class="tab-pane fade" role="tabpanel" aria-labelledby="github-tab">
{{% readfile "./github.md" %}}
  </div>
</div>

### Collect diagnostics

By default, yb-voyager captures a [diagnostics report](../diagnostics-report/) using the YugabyteDB diagnostics service that runs each time you use the yb-voyager command. If you don't want to send diagnostics when you run yb-voyager, set the [--send-diagnostics flag](../diagnostics-report/#configuration-flag) to false.

## Next step

- [Migrate](../migrate/)
