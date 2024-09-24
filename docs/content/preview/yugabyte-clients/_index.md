---
title: YugabyteDB Client shells
headerTitle: YugabyteDB Clients
linkTitle: YugabyteDB Clients
description: Use these shells to interact with YugabyteDB.
image: fa-light fa-terminal
headcontent: Shells for interacting with, configuring, and managing YugabyteDB
showRightNav: true
type: indexpage
---

YugabyteDB ships with command line interface (CLI) shells for interacting with each YugabyteDB API:

- [ysqlsh](./ysqlsh/) - The YugabyteDB SQL shell for interacting with YugabyteDB using [YSQL](../api/ysql/).
- [ycqlsh](./ycqlsh/) - The YCQL shell for interacting with YugabyteDB using [YCQL](../api/ycql/).

For information about [yugabyted](../reference/configuration/yugabyted/) and configuring [YB-Master](../reference/configuration/yb-master/) and [YB-TServer](../reference/configuration/yb-tserver/) services, refer to [Configuration](../reference/configuration/).

{{<tip title="Specifying values that have a hypen">}}
For all the command line tools, when passing in an argument with a value that starts with a hyphen (for example, `-1`), add a double hyphen (`--`) at the end of other arguments followed by the argument name and value. This tells the binary to treat those arguments as positional. For example, to specify `set_flag ysql_select_parallelism -1`, you need to do the following:

```bash
yb-ts-cli [other arguments] -- set_flag ysql_select_parallelism -1
```

{{</tip>}}

### Installation

The YugabyteDB clients are installed with YugabyteDB and located in the `bin` directory of the YugabyteDB home directory.

If you prefer, you can install a standalone version using any of the following methods:

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-bs-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linuxx86" class="nav-link" id="linuxx86-tab" data-bs-toggle="tab" role="tab" aria-controls="linuxx86" aria-selected="true">
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
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">

```sh
curl -O https://downloads.yugabyte.com/releases/{{< yb-version version="stable" >}}/yugabyte-client-{{< yb-version version="stable"  format="build">}}-darwin-x86_64.tar.gz
tar xvfz yugabyte-client-{{< yb-version version="stable"  format="build">}}-darwin-x86_64.tar.gz && cd yugabyte-client-{{< yb-version version="stable" >}}/
```

  </div>
  <div id="linuxx86" class="tab-pane fade" role="tabpanel" aria-labelledby="linuxx86-tab">

```sh
wget https://downloads.yugabyte.com/releases/{{< yb-version version="stable" >}}/yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
tar xvfz yugabyte-client-{{< yb-version version="stable"  format="build">}}-linux-x86_64.tar.gz
cd yugabyte-client-{{< yb-version version="stable" >}}
./bin/post_install.sh
```

  </div>
  <div id="linuxarm" class="tab-pane fade" role="tabpanel" aria-labelledby="linuxarm-tab">

```sh
wget https://downloads.yugabyte.com/releases/{{< yb-version version="stable" >}}/yugabyte-client-{{< yb-version version="stable" format="build">}}-linux-aarch64.tar.gz
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

&nbsp;

---

&nbsp;

{{<index/block>}}

  {{<index/item
    title="ysqlsh"
    body="Query data, create and modify database objects, and execute SQL commands and scripts using YSQL."
    href="ysqlsh/"
    icon="fa-light fa-terminal">}}

  {{<index/item
    title="ycqlsh"
    body="Query data, create and modify database objects, and execute SQL commands and scripts using YCQL."
    href="ycqlsh/"
    icon="fa-light fa-terminal">}}

{{</index/block>}}
