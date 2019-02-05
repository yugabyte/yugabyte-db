---
title: Security Checklist
linkTitle: Security Checklist
description: Security Checklist
aliases:
  - /secure/security-checklist/
menu:
  latest:
    identifier: security-checklist
    parent: secure
    weight: 705
isTocNested: true
showAsideToc: true
---

Below are a list of security measures that can be implemented to protect your YugaByte DB installation.


## Enable Authentication

Authentication requires that all clients provide valid credentials before they can connect to a YugaByte DB cluster. The authentication credentials in YugaByte DB are stored internally in the YB-Master system tables. The authentication mechanisms available to users depends on what is supported and exposed by the API (YCQL, YEDIS, YSQL).

Read more about [how to enable authentication in YugaByte DB](/secure/authentication).


## Configure Role-Based Access Control

Roles can be modified to grant users or applications only the essential privileges based on the operations they need to perform against the database. Typically, an administrator role is created first. The administrator then creates additional roles for users.

See the [authorization](/secure/authorization) section to enable role-based access control in YugaByte DB.


## Run as a dedicated user

Run the YugaByte DB processes (such as YB-Master and YB-TServer) with a dedicated operating system user account. Ensure that this dedicated user account has permissions to access the data drives, but no unnecessary permissions.


## Limit Network Exposure

### Restrict machine and port access

Ensure that YugaByte DB runs in a trusted network environment.  Here are some steps to ensure that:

* Servers running YugaByte DB processes are directly accessible only by the servers running the application and database administrators.

* Only servers running applications can connect to YugaByte DB processes on the RPC ports. Access to the various [YugaByte DB ports](/deploy/checklist/#default-ports-reference) should be denied to everybody else.


### RPC bind interfaces

Limit the interfaces on which YugaByte DB instances listen for incoming connections. Specify just the required interfaces when starting `yb-master` and `yb-tserver` by using the following `--rpc_bind_addresses` flag. Do not bind to the loopback address. Read more in the [Admin Reference](/admin/yb-tserver/) section on how to use these flags when starting the yb-master and yb-tserver processes.


### Tips for public clouds:

* Do not assign a public IP address to the nodes running YugaByte DB if possible. The applications can connect to YugaByte DB over private IP addresses.

* In AWS, run the YugaByte DB cluster in a separate VPC ([Amazon Virtual Private Network](https://docs.aws.amazon.com/vpc/latest/userguide/what-is-amazon-vpc.html)) and peer this only with VPC(s) from which database access is required, for example from those VPCs where the application will run.

* Make the security groups assigned to the database servers very restrictive. Ensure that they can communicate with each other on the necessary ports, and expose only the client accessible ports to just the required set of servers. See the [list of YugaByte DB ports](/deploy/checklist/#default-ports-reference).


## Enable encryption on the wire (Enterprise Edition)


[TLS/SSL encryption](https://en.wikipedia.org/wiki/Transport_Layer_Security) ensures that network communication between servers is secure. You can configure YugaByte DB to use TLS to encrypt intra-cluster and client to cluster network communication. It is recommended to enable TLS encryption over the wire in YugaByte DB clusters and clients to ensure privacy and integrity of data transferred over the network.

Read more about enabling [TLS/SSL encryption](/secure/tls-encryption) in YugaByte DB.



