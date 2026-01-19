---
title: Authentication methods in YugabyteDB
headerTitle: Authentication methods
linkTitle: Authentication methods
description: Verify that users and clients are who they say they are.
headcontent: Verify that users and clients are who they say they are
menu:
  v2.25:
    identifier: authentication
    parent: secure
    weight: 720
type: indexpage
---

Authentication is the process by which the database server establishes the identity of the client, and by extension determines whether the client application (or the user who runs the client application) is permitted to connect with the database user name that was requested. YugabyteDB offers a number of different client authentication methods. The method used to authenticate a particular client connection can be selected on the basis of (client) host address, database, and user.

{{< note title="Note" >}}
The authentication methods do not require any external security infrastructure and are the quickest way for YugabyteDB DBAs to secure the database. Password authentication is the easiest choice for authenticating remote user connections.
{{< /note >}}

YugabyteDB supports the following methods for authenticating users.

{{<index/block>}}

  {{<index/item
    title="Password"
    body="Authenticate using MD5 or SCRAM-SHA-256 authentication methods."
    href="password-authentication/"
    icon="fa-thin fa-lock-keyhole">}}

  {{<index/item
    title="LDAP"
    body="Use an external LDAP service to perform client authentication."
    href="ldap-authentication-ysql/"
    icon="fa-thin fa-user-lock">}}

  {{<index/item
    title="Host"
    body="Fine-grained authentication for local and remote clients based on IP addresses."
    href="host-based-authentication/"
    icon="fa-thin fa-globe">}}

  {{<index/item
    title="Trust"
    body="Allow clients to connect using a database user name."
    href="trust-authentication/"
    icon="fa-thin fa-handshake">}}

{{</index/block>}}
