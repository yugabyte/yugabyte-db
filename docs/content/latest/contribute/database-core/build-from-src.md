---
title: Building the source code
linkTitle: Building the source
description: Building the source code
image: /images/section_icons/index/quick_start.png
headcontent: Building the source code.
type: page
section: CONTRIBUTE
menu:
  latest:
    identifier: contribute-db-core-build
    parent: contribute-db-core
    weight: 2912
isTocNested: false
showAsideToc: false
---

{{< note title="Note" >}}
CentOS 7 is the main recommended development and production platform for YugaByte.
{{< /note >}}


<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#centos7" class="nav-link" id="centos7-tab" data-toggle="tab" role="tab" aria-controls="centos7" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      CentOS 7.x
    </a>
  </li>
  <li>
    <a href="#ubuntu18" class="nav-link" id="ubuntu18-tab" data-toggle="tab" role="tab" aria-controls="ubuntu18" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Ubuntu 18.04
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "build-from-src/macos.md" /%}}
  </div>
  <div id="centos7" class="tab-pane fade" role="tabpanel" aria-labelledby="centos7-tab">
    {{% includeMarkdown "build-from-src/centos7.md" /%}}
  </div> 
  <div id="ubuntu18" class="tab-pane fade" role="tabpanel" aria-labelledby="ubuntu18-tab">
    {{% includeMarkdown "build-from-src/ubuntu18.md" /%}}
  </div> 
</div>


## Building the code

Assuming this repository is checked out in `~/code/yugabyte-db`, do the following:

```bash
cd ~/code/yugabyte-db
./yb_build.sh release
```

The above command will build the release configuration, put the C++ binaries in `build/release-gcc-dynamic-community`, and will also create the `build/latest` symlink to that directory.

{{< tip title="Tip" >}}
You can find the binaries you just built in `build/latest` directory.
{{< /tip >}}


For Linux it will first make sure our custom Linuxbrew distribution is installed into `~/.linuxbrew-yb-build/linuxbrew-<version>`.


## Build Java code

YugaByte DB core is written in C++, but the repository contains Java code needed to run sample applications. To build the Java part, you need:

* JDK 8
* [Apache Maven](https://maven.apache.org/).
Also make sure Maven's bin directory is added to your PATH, e.g. by adding to your `~/.bashrc`. See the example below (if you've installed Maven into `~/tools/apache-maven-3.5.0`)

```
export PATH=$HOME/tools/apache-maven-3.5.0/bin:$PATH
```

For building YugaByte DB Java code, you'll need to install Java and Apache Maven.

