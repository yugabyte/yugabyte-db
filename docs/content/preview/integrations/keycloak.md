---
title: Keycloak
linkTitle: Keycloak
description: Use Keycloak with YSQL API
aliases:
menu:
  preview_integrations:
    identifier: keycloak
    parent: integrations-security
    weight: 571
type: docs
---

[Keycloak](https://www.keycloak.org/) is an open source identity and access management tool that adds authentication to applications and secure services with minimum effort.

## Prerequisites

Before you start using the Keycloak, ensure that you have:

- OpenJDK 21.
- The latest version of [Keycloak](https://www.keycloak.org/downloads).
- A YugabyteDB cluster. Refer to [YugabyteDB Quick start guide](/preview/tutorials/quick-start/macos/) to install and start a local cluster.

## Configure and start Keycloak

To configure Keycloak, do the following:

- Create database `keycloak` on your cluser.

    ```sh
    CREATE DATABASE keycloak;
    ```

- Add the followinf configurations in `keycloak/conf/keycloak.conf file`.

    ```sh
    # The database vendor.
    db=postgres

    # The username of the database user.
    db-username=yugabyte

    # The password of the database user.
    db-password=yugabyte

    # The full database JDBC URL. If not provided, a default URL is set based on the selected database vendor.
    db-url=jdbc:postgresql://localhost:5433/keycloak
    ```

To start Keycloak in the foreground, do the following:

```sh
$ ./bin/kc.sh start-dev
```

This may take some time. The server is ready when you see the following logs:

```output
2025-06-18 12:43:11,631 INFO  [org.keycloak.quarkus.runtime.storage.infinispan.CacheManagerFactory] (main) Starting Infinispan embedded cache manager
2025-06-18 12:43:11,691 INFO  [org.keycloak.quarkus.runtime.storage.infinispan.CacheManagerFactory] (main) JGroups JDBC_PING discovery enabled.
2025-06-18 12:43:11,810 INFO  [org.infinispan.CONTAINER] (main) Virtual threads support enabled
2025-06-18 12:43:13,127 INFO  [org.infinispan.CONTAINER] (main) ISPN000556: Starting user marshaller 'org.infinispan.commons.marshall.ImmutableProtoStreamMarshaller'
2025-06-18 12:43:13,393 INFO  [org.keycloak.connections.infinispan.DefaultInfinispanConnectionProviderFactory] (main) Node name: node_759642, Site name: null
2025-06-18 12:43:13,705 INFO  [org.keycloak.services] (main) KC-SERVICES0050: Initializing master realm
2025-06-18 12:43:16,361 INFO  [io.quarkus] (main) Keycloak 26.2.5 on JVM (powered by Quarkus 3.20.1) started in 182.882s. Listening on: http://0.0.0.0:8080
2025-06-18 12:43:16,362 INFO  [io.quarkus] (main) Profile dev activated. 
2025-06-18 12:43:16,362 INFO  [io.quarkus] (main) Installed features: [agroal, cdi, hibernate-orm, jdbc-postgresql, keycloak, narayana-jta, opentelemetry, reactive-routes, rest, rest-jackson, smallrye-context-propagation, vertx]
```

The Keycloak server should be available at `localhost:8080`.

## Verify the integration using ysqlsh

Run [ysqlsh](/preview/api/ysqlsh/) to connect to your database using the YSQL API as follows:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (15.2-YB-2.25.1.0-b0)
Type "help" for help.

yugabyte=#
```

Connect to the `keycloak` database to verify it is working as follows:

```sql
yugabyte=# \c keycloak 
You are now connected to database "keycloak" as user "yugabyte".
keycloak=# \dt
```

```output
                     List of relations
 Schema |             Name              | Type  |  Owner   
--------+-------------------------------+-------+----------
 public | admin_event_entity            | table | yugabyte
 public | associated_policy             | table | yugabyte
 public | authentication_execution      | table | yugabyte
 public | authentication_flow           | table | yugabyte
 public | authenticator_config          | table | yugabyte
 public | authenticator_config_entry    | table | yugabyte
 public | broker_link                   | table | yugabyte
 public | client                        | table | yugabyte
 public | client_attributes             | table | yugabyte
 public | client_auth_flow_bindings     | table | yugabyte
 public | client_initial_access         | table | yugabyte
 public | client_node_registrations     | table | yugabyte
 public | client_scope                  | table | yugabyte
 public | client_scope_attributes       | table | yugabyte
 public | client_scope_client           | table | yugabyte
 public | client_scope_role_mapping     | table | yugabyte
 public | component                     | table | yugabyte
 public | component_config              | table | yugabyte
 public | composite_role                | table | yugabyte
 public | credential                    | table | yugabyte
 public | databasechangelog             | table | yugabyte
 public | databasechangeloglock         | table | yugabyte
 public | default_client_scope          | table | yugabyte
 public | event_entity                  | table | yugabyte
 public | fed_user_attribute            | table | yugabyte
 public | fed_user_consent              | table | yugabyte
 public | fed_user_consent_cl_scope     | table | yugabyte
 public | fed_user_credential           | table | yugabyte
 public | fed_user_group_membership     | table | yugabyte
 public | fed_user_required_action      | table | yugabyte
 public | fed_user_role_mapping         | table | yugabyte
 public | federated_identity            | table | yugabyte
 public | federated_user                | table | yugabyte
 public | group_attribute               | table | yugabyte
 public | group_role_mapping            | table | yugabyte
 public | identity_provider             | table | yugabyte
 public | identity_provider_config      | table | yugabyte
 public | identity_provider_mapper      | table | yugabyte
 public | idp_mapper_config             | table | yugabyte
 public | jgroups_ping                  | table | yugabyte
 public | keycloak_group                | table | yugabyte
 public | keycloak_role                 | table | yugabyte
 public | migration_model               | table | yugabyte
 public | offline_client_session        | table | yugabyte
 public | offline_user_session          | table | yugabyte
 public | org                           | table | yugabyte
 public | org_domain                    | table | yugabyte
 public | policy_config                 | table | yugabyte
 public | protocol_mapper               | table | yugabyte
 public | protocol_mapper_config        | table | yugabyte
 public | realm                         | table | yugabyte
 public | realm_attribute               | table | yugabyte
 public | realm_default_groups          | table | yugabyte
 public | realm_enabled_event_types     | table | yugabyte
 public | realm_events_listeners        | table | yugabyte
 public | realm_localizations           | table | yugabyte
 public | realm_required_credential     | table | yugabyte
 public | realm_smtp_config             | table | yugabyte
 public | realm_supported_locales       | table | yugabyte
 public | redirect_uris                 | table | yugabyte
 public | required_action_config        | table | yugabyte
 public | required_action_provider      | table | yugabyte
 public | resource_attribute            | table | yugabyte
 public | resource_policy               | table | yugabyte
 public | resource_scope                | table | yugabyte
 public | resource_server               | table | yugabyte
 public | resource_server_perm_ticket   | table | yugabyte
 public | resource_server_policy        | table | yugabyte
 public | resource_server_resource      | table | yugabyte
 public | resource_server_scope         | table | yugabyte
 public | resource_uris                 | table | yugabyte
 public | revoked_token                 | table | yugabyte
 public | role_attribute                | table | yugabyte
 public | scope_mapping                 | table | yugabyte
 public | scope_policy                  | table | yugabyte
 public | server_config                 | table | yugabyte
 public | user_attribute                | table | yugabyte
 public | user_consent                  | table | yugabyte
 public | user_consent_client_scope     | table | yugabyte
 public | user_entity                   | table | yugabyte
 public | user_federation_config        | table | yugabyte
 public | user_federation_mapper        | table | yugabyte
 public | user_federation_mapper_config | table | yugabyte
 public | user_federation_provider      | table | yugabyte
 public | user_group_membership         | table | yugabyte
 public | user_required_action          | table | yugabyte
 public | user_role_mapping             | table | yugabyte
 public | web_origins                   | table | yugabyte
```

## Test Keycloak

Go to localhost:8080 and you will see the dashboard to create a user. Enter your desired username and the password. 

![Keycloak Create User](/images/develop/ecosystem-integrations/keycloak/keycloak-create-user.png)

Once the user is created, you will see the following screen. Click on the `Open Administration Console` button.

![Keycloak Open Admin Console](/images/develop/ecosystem-integrations/keycloak/keycloak-open-admin-console.png)

You can also verify the user creation on your database:

```sql
keycloak=# select * from user_entity ;
                  id                  | email |           email_constraint           | email_verified | enabled | federation_link | first_name | last_name |               realm_id               |   username    | created_timestamp | service_account_client_link | not_before 
--------------------------------------+-------+--------------------------------------+----------------+---------+-----------------+------------+-----------+--------------------------------------+---------------+-------------------+-----------------------------+------------
 5fa4b561-66e1-49ab-9a85-bbb4d6088f15 |       | 6b1734a9-ff97-497e-9082-e8dd6698d4d8 | f              | t       |                 |            |           | 099df43b-6d65-471f-b699-9aa9450cd833 | keycloak_user |     1750230947560 |                             |          0
(1 row)

```

On the sign in page, enter the username and password you just created.:

![Keycloak Sign In](/images/develop/ecosystem-integrations/keycloak/keycloak-sign-in.png)

You are now logged in to the Keycloak dashboard:

![Keycloak Dashboard](/images/develop/ecosystem-integrations/keycloak/keycloak-welcome-page.png)