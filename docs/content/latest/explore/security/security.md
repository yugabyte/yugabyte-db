---
title: Security
headerTitle: Security
linkTitle: Security
description: Overview of security in YugabyteDB.
headcontent: Security in YugabyteDB.
image: /images/section_icons/index/secure.png
aliases:
  - /latest/explore/security/
menu:
  latest:
    identifier: explore-security
    parent: explore
    weight: 930
isTocNested: true
showAsideToc: true
---

Like PostgreSQL, YugabyteDB provides security in multiple ways:

* **Authentication** - limit access to the database to clients with proper credentials

* **Authorization** - create users and roles, and grant privileges to restrict activities that the users and roles can perform

* **Encryption** - encrypt the database, as well as all network communication between servers

* **Auditing** - conduct session- and object-level auditing

* **Restricting network access** - limit connections to the database using RPC binding

## Authentication
 
Using client authentication, you can define how the database server establishes the identity of the client, and whether the client application (or the user who runs the client application) is allowed to connect with the database user name that was requested. YugabyteDB offers a number of different client authentication methods, including the following:

* Password - authenticate using MD5 or SCRAM-SHA-256
* LDAP - use external LDAP services to perform client authentication
* Host-based - authenticate local and remote clients based on IP address and using TLS certiciates
* Trust - authorize specific local connections

The method used to authenticate a particular client connection can be selected on the basis of (client) host address, database, and user.

The authentication credentials in YugabyteDB are stored internally in the YB-Master system tables. The authentication mechanisms available to clients depends on what is supported and exposed by the YSQL, YCQL, and YEDIS APIs.

Read more about [how to enable authentication in YugabyteDB](../../secure/authentication).

## Authorization

YugabyteDB provides role-based access control, consisting of a collection of privileges on resources given to roles. 

Read more about [authorization in YugabyteDB](../../secure/authorization).

### Roles

Roles are essential to implementing and administering access control on a YugabyteDB cluster. Roles can represent individual users or a group of users, and encapsulate a set of privileges that can be assigned to other roles (or users). Roles can be modified to grant users or applications only the essential privileges based on the operations they need to perform against the database. Typically, an administrator role is created first. The administrator then creates additional roles for users.  

### Privileges

Roles are explicitly granted privileges to access objects in the database using the `GRANT` command. You can, for example, assign read access to one role, data modify access to another role, and alter table access to a third.

By default, when an object is created, only the owner has privileges, and other roles must be granted privileges.

### Row-level access

In addition to database access permissions available through the `ROLE` and `GRANT` privilege system, YugabyteDB provides a more granular level of security where tables can have row security policies that restrict the rows that users can access.

Row-level Security (RLS) restricts rows that can be returned by normal queries or inserted, updated, or deleted by DML commands. Row-level security policies can be created specific to a `DML` command or with `ALL` commands. They can also be used to create policies on a particular role or multiple roles.

By default, tables do not have any row level policies defined, so that if a user has access privileges to a table, all rows within the table are available to query and update.

### Column-level access

Column-level security in YugabyteDB is used to restrict users to viewing only a particular column or set of columns in a table. You do this by creating a view that includes only the columns that the user needs access to using the `CREATE VIEW` command.

## Encryption

YugabyteDB supports encryption both of clusters, and all network communication between servers. 

### Encryption in transit

[TLS encryption](https://en.wikipedia.org/wiki/Transport_Layer_Security) ensures that network communication between servers is secure. You can configure YugabyteDB to use TLS to encrypt intra-cluster and client to server network communication. Servers are secured using TLS certificates, which can be from a public CA or self-signed.

You should enable encryption in transit for YugabyteDB clusters and clients to ensure privacy and integrity of data transferred over the network.

Read more about enabling [Encryption in transit](../../secure/tls-encryption) in YugabyteDB.

### Encryption at rest

[Encryption at rest](https://en.wikipedia.org/wiki/Data_at_rest#Encryption) ensures that data at rest, i.e., stored on disk, is protected. You can configure YugabyteDB with a user-generated symmetric key to perform cluster-wide encryption.

Read more about enabling [Encryption at rest](../../secure/encryption-at-rest) in YugabyteDB.

### Column-level encryption

YugabyteDB provides column-level encryption to restrict access to sensitive data such as addresses and credit card details. YugabyteDB uses the PostgreSQL `pgcrypto` extension to enable column level encryption. The `PGP_SYM_ENCRYPT` and `PGP_SYM_DECRYPT` functions of `pgcrypto` are used to encrypt and decrypt column data.

Read more about enabling [column-level encryption](../../secure/column-level-encryption) in YugabyteDB.

## Auditing

The goal of audit logging is to enable you to produce audit logs often required to comply with government, financial, or ISO certifications. YugabyteDB YSQL uses the PostgreSQL Audit Extension (`pgAudit`) to provide detailed session and/or object audit logging via YugabyteDB TServer logging.

Read more about [audit logging](../../secure/audit-logging) in YugabyteDB.

### Session

Session logging is enabled on a per user session basis. You can enable session logging for all DML and DDL statements and log all relations in DML statements.

### Object

Object logging logs statements that affect a particular relation, and is intended to be a finer-grained replacement for session-level logging. As such, it may not make sense to use them in conjunction but one possible scenario would be to use session logging to capture each statement and then supplement that with object logging to get more detail about specific relations.

In YugabyteDB, object-level audit logging is implemented by reusing the PG role system. The `pgaudit.role` setting defines the role that will be used for audit logging. A relation (TABLE, VIEW, etc.) will be audit logged when the audit role has permissions for the command executed or inherits the permissions from another role. This allows you to effectively have multiple audit roles even though there is a single master role in any context.

## Restricting network access

Ensure that YugabyteDB runs in a trusted network environment, such that:

* Servers running YugabyteDB services are directly accessible only by the servers running the application and database administrators.

* Only servers running applications can connect to YugabyteDB services on the RPC ports. Access to the [YugabyteDB ports](../../deploy/checklist/#default-ports-reference) should be denied to everybody else.

In addition, you can limit the interfaces on which YugabyteDB instances listen for incoming connections. To specify just the required interfaces when starting `yb-master` and `yb-tserver`, use the `--rpc_bind_addresses` option. Do not bind to the loopback address. Read more in the [Admin Reference](../../reference/configuration/yb-tserver/) section on how to use these options when starting `yb-master` and `yb-tserver` services.
