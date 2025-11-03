---
title: Role-based access control in YugabyteDB
headerTitle: Role-based access control
linkTitle: Role-based access control
description: Enable authorization using role-based access control in YugabyteDB.
headcontent: Authorize users using role-based access control
aliases:
  - /secure/authorization/
menu:
  stable:
    identifier: authorization
    parent: secure
    weight: 722
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Overview"
    body="Understanding role-based access control (RBAC)."
    href="rbac-model/"
    icon="fa-thin fa-user-group-crown">}}

  {{<index/item
    title="Create roles"
    body="Create one or more roles."
    href="create-roles/"
    icon="fa-thin fa-user-group">}}

  {{<index/item
    title="Grant privileges"
    body="Grant privileges to users and roles."
    href="ysql-grant-permissions/"
    icon="fa-thin fa-person-circle-check">}}

  {{<index/item
    title="Row-level security"
    body="Using row-level security (RLS) policies in YugabyteDB."
    href="row-level-security/"
    icon="fa-thin fa-table-cells-row-lock">}}

  {{<index/item
    title="Column-level security"
    body="Restricting column-level permissions in YugabyteDB."
    href="column-level-security/"
    icon="fa-thin fa-table-cells-column-lock">}}

{{</index/block>}}
