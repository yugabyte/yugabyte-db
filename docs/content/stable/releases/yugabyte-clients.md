---
title: YugabyteDB Client shells
headerTitle: YugabyteDB Clients
linkTitle: YugabyteDB Clients
description: Use these shells to interact with YugabyteDB
headcontent: Shells for interacting with, configuring, and managing YugabyteDB
image: fa-thin fa-terminal
type: docs
---

YugabyteDB ships with command line interface (CLI) shells for interacting with each YugabyteDB API.

| Client | API | Description |
| :--- | :--- | :--- |
| [ysqlsh](../../api/ysqlsh/) | [YSQL](../../api/ysql/) | SQL shell for interacting with YugabyteDB using PostgreSQL-compatible YSQL API. |
| [ycqlsh](../../api/ycqlsh/) | [YCQL](../../api/ycql/) | CQL shell for interacting with YugabyteDB using Cassandra-compatible YCQL API. |

## Compatibility

Clients work best with servers of the same or an older major version. If you are running multiple versions of YugabyteDB, use the newest version of the client to connect. You can keep and use the matching version of a client to use with each version of YugabyteDB, but in practice, this shouldn't be necessary. For best results, use the latest client.

While the general functionality of running SQL statements and displaying query results should also work with servers of a newer major version, this cannot be guaranteed in all cases. In particular, due to the change from PostgreSQL 11 to 15 in YugabyteDB 2.25, use only the latest client to avoid compatibility issues.

## Installation

The YugabyteDB clients are installed with [YugabyteDB](../ybdb-releases/) and located in the `bin` directory of the YugabyteDB home directory.

You can also install a standalone version using any of the following methods:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#linuxx86" class="nav-link active" id="linuxx86-tab" data-bs-toggle="tab" role="tab" aria-controls="linuxx86" aria-selected="true">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Linux x86
    </a>
  </li>
  <li>
    <a href="#linuxarm" class="nav-link" id="linuxarm-tab" data-bs-toggle="tab" role="tab" aria-controls="linuxarm" aria-selected="true">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Linux ARM
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-bs-toggle="tab" role="tab" aria-controls="docker" aria-selected="true">
      <i class="fa-brands fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="linuxx86" class="tab-pane fade show active" role="tabpanel" aria-labelledby="linuxx86-tab">

```sh
wget https://downloads.yugabyte.com/releases/{{< yb-version version="stable" >}}/yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
echo "$(curl -L https://downloads.yugabyte.com/releases/{{< yb-version version="stable" >}}/yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-x86_64-tar.gz.sha) *yugabyte-client-{{< yb-version version="stable"  format="build">}}-linux-x86_64.tar.gz" | shasum --check && \
tar xvfz yugabyte-client-{{< yb-version version="stable"  format="build">}}-linux-x86_64.tar.gz
cd yugabyte-client-{{< yb-version version="stable" >}}
./bin/post_install.sh
```

  </div>
  <div id="linuxarm" class="tab-pane fade" role="tabpanel" aria-labelledby="linuxarm-tab">

```sh
wget https://downloads.yugabyte.com/releases/{{< yb-version version="stable" >}}/yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-aarch64.tar.gz
echo "$(curl -L https://downloads.yugabyte.com/releases/{{< yb-version version="stable" >}}/yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-aarch64-tar.gz.sha) *yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-aarch64.tar.gz" | shasum --check && \
tar xvfz yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-aarch64.tar.gz
cd yugabyte-client-{{< yb-version version="stable" >}}
./bin/post_install.sh
```

  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">

```sh
docker pull yugabytedb/yugabyte-client:latest
```

  </div>
</div>

## Release notes

The YugabyteDB clients are released with every version of [YugabyteDB](../ybdb-releases/). Only versions with client-specific changes are listed.

### v2025.2.1.0 - February 12, 2025 {#v2025.2.1.0}

* Updated ycqlsh Python compatibility. ycqlsh now requires Python 3 v3.6 or later.

### v2.25.0.0 - January 17, 2025 {#v2.25.0.0}

* Updated ysqlsh for PostgreSQL 15 compatibility.

### v2.23.0.0 - September 13, 2024 {#v2.23.0.0}

* Documents the limitations of retry logic when using `-c` flag in ysqlsh command. {{<issue 21804>}}

* Allows the deletion of the Cassandra role in ycqlsh without it regenerating upon cluster restart, by adding a flag to mark if the role was previously created. {{<issue 21057>}}

### v2024.1.1.0 - July 31, 2024 {#v2024.1.1.0}

* Automated SQL/CQL Shell binary. Along with full binary, added separate downloadable SQL/CQL Shell binary. <!-- IDEA-1526 -->
