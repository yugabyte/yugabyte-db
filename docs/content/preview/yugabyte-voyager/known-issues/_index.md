---
title: Manual review guideline for YugabyteDB Voyager
headerTitle: Manual review guideline
linkTitle: Manual review guideline
image: /images/section_icons/develop/learn.png
headcontent: Unsupported features and a guide with workarounds when migrating data using YugabyteDB Voyager.
description: Manual guideline and suggested workarounds for migrating data using YugabyteDB Voyager.
type: indexpage
showRightNav: true
menu:
  preview_yugabyte-voyager:
    identifier: known-issues
    parent: yugabytedb-voyager
    weight: 104
---

The following sections include a table of unsupported features, and a manual guide with necessary workarounds you can review when migrating data with YugabyteDB Voyager.

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub Issue |
| :------ | :------------------------ | :----------- |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |
| USERS/GRANTS | Voyager does not support migrating the USERS and GRANTS from the source database to the target cluster. |
| Unsupported datatypes | Data migration is unsupported for some datatypes such as BLOB and XML. For others such as ANY and BFile, both schema and data migration is unsupported. Refer to [datatype mapping](../reference/datatype-mapping-oracle/) for the detailed list of datatypes. | |

## Manual review guideline

<div class="row">
 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="general-issues/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true" />
        <div class="title">General</div>
      </div>
      <div class="body">
       Guide for MySQL, PostgreSQL, and Oracle source databases.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="mysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true" />
        <div class="title">MySQL</div>
      </div>
      <div class="body">
       Guide for MySQL as the source database.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="postgresql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true" />
        <div class="title">PostgreSQL</div>
      </div>
      <div class="body">
       Guide for Oracle as the source database.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="oracle/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true" />
        <div class="title">Oracle</div>
      </div>
      <div class="body">
        Guide for Oracle as the source database.
      </div>
    </a>
  </div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="mysql-oracle/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true" />
        <div class="title">MySQL and Oracle</div>
      </div>
      <div class="body">
       Guide for MySQL and Oracle as the source databases.
      </div>
    </a>
  </div>
</div>
