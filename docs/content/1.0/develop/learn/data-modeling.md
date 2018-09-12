---
title: 2. Data Modeling
linkTitle: 2. Data Modeling
description: Data Modeling
aliases:
  - /develop/learn/data-modeling/
menu:
  1.0:
    identifier: data-modeling
    parent: learn
    weight: 562
---

Data modeling is a process that involves identifying the entities (items to be stored) and the relationships between entities. To create your data model, identify the patterns used to access data and the types of queries to be performed. These two ideas inform the organization and structure of the data, and the design and creation of the database's tables.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#cassandra" class="nav-link active" id="cassandra-tab" data-toggle="tab" role="tab" aria-controls="cassandra" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      Cassandra
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cassandra" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cassandra-tab">
    {{% includeMarkdown "cassandra/data-modeling.md" /%}}
  </div>
</div>
