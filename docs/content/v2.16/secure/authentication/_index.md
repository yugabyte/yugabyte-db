---
title: Authentication methods in YugabyteDB
headerTitle: Authentication methods
linkTitle: Authentication methods
description: Verify that users and clients are who they say they are.
headcontent: Verify that users and clients are who they say they are.
image: /images/section_icons/secure/authorization.png
menu:
  v2.16:
    identifier: authentication
    parent: secure
    weight: 720
type: indexpage
---

Authentication is the process by which the database server establishes the identity of the client, and by extension determines whether the client application (or the user who runs the client application) is permitted to connect with the database user name that was requested. YugabyteDB offers a number of different client authentication methods. The method used to authenticate a particular client connection can be selected on the basis of (client) host address, database, and user.

{{< note title="Note" >}}
The authentication methods do not require any external security infrastructure and are the quickest way for YugabyteDB DBAs to secure the database. Password authentication is the easiest choice for authenticating remote user connections.
{{< /note >}}

The various methods for authenticating users supported by YugabyteDB are listed below.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="password-authentication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">Password authentication</div>
      </div>
      <div class="body">
          Authenticate using <code>MD5</code> or <code>SCRAM-SHA-256</code> authentication methods.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ldap-authentication-ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">LDAP authentication</div>
      </div>
      <div class="body">
          Use an external LDAP service to perform client authentication.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="host-based-authentication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">Host-based authentication</div>
      </div>
      <div class="body">
        Fine-grained authentication for local and remote clients based on IP addresses.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="trust-authentication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">Trust authentication</div>
      </div>
      <div class="body">
          Allow clients to connect using a database user name.
      </div>
    </a>
  </div>

</div>
