---
date: 2016-03-09T00:11:02+01:00
title: Quick Start
weight: 4
---

 The easiest way to get started with the YugaByte Community Edition is to create a multi-node **local cluster** on your laptop or desktop. There are two options available.

<ul class="nav nav-tabs">
  <li class="active">
    <a data-toggle="tab" href="#binary">
      Local Cluster - Binary
    </a>
  </li>
  <li>
    <a data-toggle="tab" href="#docker">
      Local Cluster - Docker
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="binary" class="tab-pane fade in active">
    {{% includeMarkdown "community-edition/quick-start/binary.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade">
    {{% includeMarkdown "community-edition/quick-start/docker.md" /%}}
  </div>
</div>


{{< note title="Note" >}}
Running local clusters is not recommended for production environments. You can either deploy the [Community Edition] (/community-edition/deploy/) manually on a set of instances or use the [Enterprise Edition](/enterprise-edition/deploy/) that automates all day-to-day operations including cluster administration across all major public clouds as well as on-premises datacenters.
{{< /note >}}

