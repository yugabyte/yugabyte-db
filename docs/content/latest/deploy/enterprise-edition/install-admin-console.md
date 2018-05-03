---
title: Install Admin Console
linkTitle: 2. Install Admin Console
description: Install Admin Console
aliases:
  - /deploy/enterprise-edition/admin-console/
  - /deploy/enterprise-edition/install-admin-console/
menu:
  latest:
    identifier: install-admin-console
    parent: deploy-enterprise-edition
    weight: 670
---

An “airgapped” host has no path to inbound or outbound Internet traffic at all. In order to install Replicated and YugaWare on such a host, we first download the binaries on a machine that has Internet connectivity and then copy the files over to the appropriate host.


<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#default" class="nav-link active" id="default-install" data-toggle="tab" role="tab" aria-controls="default-install" aria-selected="true">
      <i class="icon-aws" aria-hidden="true"></i>
      Default
    </a>
  </li>
  <li>
    <a href="#air-gapped" class="nav-link" id="air-gapped" data-toggle="tab" role="tab" aria-controls="air-gapped" aria-selected="true">
      <i class="icon-google" aria-hidden="true"></i>
      Airgapped
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="default-install" class="tab-pane fade show active" role="tabpanel" aria-labelledby="default-install">
    {{% includeMarkdown "install-admin-console/default.md" /%}}
  </div>
  <div id="air-gapped" class="tab-pane fade" role="tabpanel" aria-labelledby="air-gapped">
    {{% includeMarkdown "install-admin-console/air-gapped.md" /%}}
  </div>
</div>
