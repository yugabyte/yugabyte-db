---
title: RBAC model
linkTitle: RBAC model
description: RBAC model
headcontent: How role-based access control works
image: /images/section_icons/secure/rbac-model.png
menu:
  latest:
    identifier: rbac-model
    parent: authorization
    weight: 716
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#ysql" class="nav-link active" id="ycql-tab" data-toggle="tab" role="tab" aria-controls="ysql" aria-selected="true">
      <i class="icon-ysql" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li>
    <a href="#ycql" class="nav-link" id="ycql-tab" data-toggle="tab" role="tab" aria-controls="ycql" aria-selected="false">
      <i class="icon-ycql" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ysql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ysql-tab">
    {{% includeMarkdown "ysql-rbac-model.md" /%}}
  </div>
  <div id="ycql" class="tab-pane fade" role="tabpanel" aria-labelledby="ycql-tab">
    {{% includeMarkdown "ycql-rbac-model.md" /%}}
  </div>
</div>
