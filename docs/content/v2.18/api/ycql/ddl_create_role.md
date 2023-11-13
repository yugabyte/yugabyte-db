---
title: CREATE ROLE statement [YCQL]
headerTitle: CREATE ROLE
linkTitle: CREATE ROLE
description: Use the `CREATE ROLE` statement to create a new role that is used to authenticate into YCQL and as a group of permissions used to restrict operations on the database objects.
menu:
  v2.18:
    parent: api-cassandra
    weight: 1235
type: docs
---

## Synopsis

Use the `CREATE ROLE` statement to create a new role that is used to authenticate into YCQL and as a group of permissions is used to restrict operations on the database objects. Note that users are specific roles that are login enabled. There is no explicit `CREATE USER` command in YCQL.

This statement is enabled by setting the YB-TServer flag [`--use_cassandra_authentication`](../../../reference/configuration/yb-tserver/#use-cassandra-authentication) to `true`.

## Syntax

### Diagram

#### create_role

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="730" height="80" viewbox="0 0 730 80"><path class="connector" d="M0 52h15m68 0h10m53 0h30m32 0h10m46 0h10m64 0h20m-197 0q5 0 5 5v8q0 5 5 5h172q5 0 5-5v-8q0-5 5-5m5 0h10m87 0h30m54 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h35m46 0h35q5 0 5 5v20q0 5-5 5m-5 0h40m-245 0q5 0 5 5v8q0 5 5 5h220q5 0 5-5v-8q0-5 5-5m5 0h15"/><polygon points="0,59 5,52 0,45" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="35" width="68" height="25" rx="7"/><text class="text" x="25" y="52">CREATE</text><rect class="literal" x="93" y="35" width="53" height="25" rx="7"/><text class="text" x="103" y="52">ROLE</text><rect class="literal" x="176" y="35" width="32" height="25" rx="7"/><text class="text" x="186" y="52">IF</text><rect class="literal" x="218" y="35" width="46" height="25" rx="7"/><text class="text" x="228" y="52">NOT</text><rect class="literal" x="274" y="35" width="64" height="25" rx="7"/><text class="text" x="284" y="52">EXISTS</text><a xlink:href="../grammar_diagrams#role-name"><rect class="rule" x="368" y="35" width="87" height="25"/><text class="text" x="378" y="52">role_name</text></a><rect class="literal" x="485" y="35" width="54" height="25" rx="7"/><text class="text" x="495" y="52">WITH</text><rect class="literal" x="599" y="5" width="46" height="25" rx="7"/><text class="text" x="609" y="22">AND</text><a xlink:href="../grammar_diagrams#role-property"><rect class="rule" x="569" y="35" width="106" height="25"/><text class="text" x="579" y="52">role_property</text></a><polygon points="726,59 730,59 730,45 726,45" style="fill:black;stroke-width:0"/></svg>

#### role_property

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="321" height="95" viewbox="0 0 321 95"><path class="connector" d="M0 22h25m89 0h10m30 0h10m107 0h45m-301 25q0 5 5 5h5m59 0h10m30 0h10m128 0h39q5 0 5-5m-296-25q5 0 5 5v50q0 5 5 5h5m93 0h10m30 0h10m128 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="89" height="25" rx="7"/><text class="text" x="35" y="22">PASSWORD</text><rect class="literal" x="124" y="5" width="30" height="25" rx="7"/><text class="text" x="134" y="22">=</text><rect class="literal" x="164" y="5" width="107" height="25" rx="7"/><text class="text" x="174" y="22">&lt;Text Literal&gt;</text><rect class="literal" x="25" y="35" width="59" height="25" rx="7"/><text class="text" x="35" y="52">LOGIN</text><rect class="literal" x="94" y="35" width="30" height="25" rx="7"/><text class="text" x="104" y="52">=</text><rect class="literal" x="134" y="35" width="128" height="25" rx="7"/><text class="text" x="144" y="52">&lt;Boolean Literal&gt;</text><rect class="literal" x="25" y="65" width="93" height="25" rx="7"/><text class="text" x="35" y="82">SUPERUSER</text><rect class="literal" x="128" y="65" width="30" height="25" rx="7"/><text class="text" x="138" y="82">=</text><rect class="literal" x="168" y="65" width="128" height="25" rx="7"/><text class="text" x="178" y="82">&lt;Boolean Literal&gt;</text></svg>

### Grammar

```ebnf
create_role ::= CREATE ROLE [ IF NOT EXISTS ] role_name 
                [ WITH role_property [ AND ... ] ]

role_property ::=  PASSWORD = <Text Literal>
                 | LOGIN = <Boolean Literal>
                 | SUPERUSER = <Boolean Literal>
```

Where

- `role_name` is a text identifier.

## Semantics

- An error is raised if `role_name` already exists unless the `IF NOT EXISTS` option is used.
- By default, a role does not possess the `LOGIN` privilege nor `SUPERUSER` status.
- A role with the `SUPERUSER` status possesses all the permissions on all the objects in the database even though they are not explicitly granted.
- Only a role with the `SUPERUSER` status can create another `SUPERUSER` role.
- A role with the `LOGIN` privilege can be used to authenticate into YQL.
- Only a client with the permission `CREATE` on `ALL ROLES` or with the `SUPERUSER` status can create another role.

## Examples

### Create a simple role with no properties

```sql
ycqlsh:example> CREATE ROLE role1;
```

### Create a `SUPERUSER` role

```sql
ycqlsh:example> CREATE ROLE role2 WITH SUPERUSER = true;
```

### Create a regular user with ability to log in

You can create a regular user with login privileges as shown below. Note the `SUPERUSER` set to `false`.

```sql
ycqlsh:example> CREATE ROLE role3 WITH SUPERUSER = false AND LOGIN = true AND PASSWORD = 'aid8134'
```

## See also

- [`ALTER ROLE`](../ddl_alter_role)
- [`DROP ROLE`](../ddl_drop_role)
- [`GRANT ROLE`](../ddl_grant_role)
- [`REVOKE ROLE`](../ddl_revoke_role)
- [`GRANT PERMISSION`](../ddl_grant_permission)
- [`REVOKE PERMISSION`](../ddl_revoke_permission)
