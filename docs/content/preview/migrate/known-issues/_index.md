---
title: Known issues
headerTitle: Known issues and workarounds
linkTitle: Known issues
image: /images/section_icons/develop/learn.png
headcontent: Unsupported features and known issues with workarounds when migrating data using YugabyteDB Voyager.
type: indexpage
showRightNav: true
menu:
  preview:
    identifier: known-issues
    parent: voyager
    weight: 104
---

This section documents unsupported features as well as known issues and workarounds when migrating data with YugabyteDB Voyager.

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub Issue |
| :------ | :------------------------ | :----------- |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |
| BLOB and CLOB | yb-voyager currently ignores all columns of type BLOB/CLOB. <br>Use another mechanism to load the attributes.| [43](https://github.com/yugabyte/yb-voyager/issues/43) |

## Known issues

<div class="row">
 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="general-issues/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true" />
        <div class="title">General known issues</div>
      </div>
      <div class="body">
       Known issues common to MySQL, PostgreSQL, and Oracle source databases.
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
        Known issues with MySQL as the source database.
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
        Known issues with PostgreSQL as the source database.
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
       Known issues with Oracle as the source database.
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
       Known issues common to MySQL and Oracle source databases.
      </div>
    </a>
  </div>
</div>
