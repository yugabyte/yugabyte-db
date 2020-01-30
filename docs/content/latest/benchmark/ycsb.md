---
title: YCSB
linkTitle: YCSB
description: YCSB
image: /images/section_icons/architecture/concepts.png
headcontent: Benchmark YugabyteDB using YCSB.
menu:
  latest:
    identifier: benchmark-ycsb
    parent: benchmark
    weight: 740
aliases:
  - /benchmark/ycsb/
showAsideToc: True
isTocNested: True
---

{{< note title="Note" >}}

For more information about YCSB see: 

* YCSB Wiki: https://github.com/brianfrankcooper/YCSB/wiki
* Workload info: https://github.com/brianfrankcooper/YCSB/wiki/Core-Workloads

{{< /note >}}

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#yugabytesql" class="nav-link" id="yugabytesql-tab" data-toggle="tab" role="tab" aria-controls="yugabytesql" aria-selected="true">
      YSQL
    </a>
  </li>
  <li >
    <a href="#yugabytecql" class="nav-link active" id="yugabytecql-tab" data-toggle="tab" role="tab" aria-controls="yugabytecql" aria-selected="false">
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="yugabytesql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabytesql-tab">
    {{% includeMarkdown "ycsb/yugabytesql.md" /%}}
  </div> 
  <div id="yugabytecql" class="tab-pane fade" role="tabpanel" aria-labelledby="yugabytecql-tab">
    {{% includeMarkdown "ycsb/yugabytecql.md" /%}}
  </div>
</div>
