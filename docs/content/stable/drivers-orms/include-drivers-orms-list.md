<!--
+++
private = true
+++
-->

{{< tabpane text=true >}}

  {{% tab header="Java" lang="java" %}}

|            | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| **Drivers** | | | |
| YugabyteDB JDBC Smart Driver<br/>[Recommended] | [42.3.5-yb-5](https://mvnrepository.com/artifact/com.yugabyte/jdbc-yugabytedb/42.3.5-yb-5) | Full | [CRUD](/preview/drivers-orms/java/yugabyte-jdbc/) |
| YugabyteDB R2DBC Smart Driver | [1.1.0-yb-1-ea](https://mvnrepository.com/artifact/com.yugabyte/r2dbc-postgresql) | Full | [CRUD](/preview/drivers-orms/java/yb-r2dbc/) |
| PostgreSQL JDBC Driver  | [42.3.4](https://mvnrepository.com/artifact/org.postgresql/postgresql/42.3.4) | Full | [CRUD](/preview/drivers-orms/java/postgres-jdbc/) |
| Vert.x Pg Client       | [4.3.2](https://mvnrepository.com/artifact/io.vertx/vertx-core/4.3.2) | Full | [CRUD](/preview/drivers-orms/java/ysql-vertx-pg-client/) |
| YugabyteDB Java Driver for YCQL | [3.10.3-yb-2](https://mvnrepository.com/artifact/com.yugabyte/cassandra-driver-core/3.10.3-yb-2) | Full | [CRUD](/preview/drivers-orms/java/ycql) |
| YugabyteDB Java Driver for YCQL | [4.15.0-yb-1](https://mvnrepository.com/artifact/com.yugabyte/java-driver-core/4.15.0-yb-1) | Full | [CRUD](/preview/drivers-orms/java/ycql-4.x) |
| **ORMs** | | | |
| Ebean                   | 13.23.0 | Full | [CRUD](/preview/drivers-orms/java/ebean/) |
| Hibernate               | 5.4.19  | Full | [CRUD](/preview/drivers-orms/java/hibernate/) |
| Spring Data YugabyteDB  | 2.3.0   | Full | [CRUD](/preview/integrations/spring-framework/sdyb/#examples) |
| Spring Data JPA         | 2.6.3   | Full | [CRUD](/preview/integrations/spring-framework/sd-jpa/#fundamentals) |
| MyBatis                 | 3.5.9   | Full | [CRUD](/preview/drivers-orms/java/mybatis/) |
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->

  {{% /tab %}}

  {{% tab header="Go" lang="go" %}}

|            | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| **Drivers** | | | |
| YugabyteDB PGX Smart Driver<br/>[Recommended] | [v4](https://pkg.go.dev/github.com/yugabyte/pgx/) | Full | [CRUD](/preview/drivers-orms/go/yb-pgx/) |
| PGX Driver | [v4](https://pkg.go.dev/github.com/jackc/pgx/) | Full | [CRUD](/preview/drivers-orms/go/pgx/) |
| PQ Driver  | [v1.10.2](https://github.com/lib/pq/releases/tag/v1.10.2/) | Full | [CRUD](/preview/drivers-orms/go/pq/) |
| YugabyteDB Go Driver for YCQL | [3.16.3](https://github.com/yugabyte/gocql) | Full | [CRUD](/preview/drivers-orms/go/ycql) |
| **ORMs** | | | |
| GORM       | [1.9.16](https://github.com/go-gorm/gorm) | Full | [CRUD](/preview/drivers-orms/go/gorm/) |
| PG         | [10](https://github.com/go-pg/pg) | Full | [CRUD](/preview/drivers-orms/go/pg/) |

  {{% /tab %}}

  {{% tab header="Python" lang="python" %}}

|            | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| **Drivers** | | | |
| YugabyteDB Psycopg2 Smart Driver<br/>[Recommended] | [2.9.3](https://github.com/yugabyte/psycopg2) |Full | [CRUD](/preview/drivers-orms/python/yugabyte-psycopg2/) |
| PostgreSQL Psycopg2 Driver | [2.9.3](https://github.com/psycopg/psycopg2) | Full | [CRUD](/preview/drivers-orms/python/postgres-psycopg2/) |
| aiopg      | 1.4          | Full | [Hello World](/preview/drivers-orms/python/aiopg/) |
| YugabyteDB Python Driver for YCQL | [3.25.0](https://github.com/yugabyte/cassandra-python-driver/tree/master) | Full | [CRUD](/preview/drivers-orms/python/ycql/) |
| **ORMs** | | | |
| Django     | 2.2 or later | Full | [CRUD](/preview/drivers-orms/python/django/) |
| SQLAlchemy | 2.0          | Full | [CRUD](/preview/drivers-orms/python/sqlalchemy/) |

  {{% /tab %}}

  {{% tab header="NodeJS" lang="nodejs" %}}

|            | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| **Drivers** | | | |
| YugabyteDB node-postgres Smart Driver<br/>[Recommended] | [8.7.3-yb-1](https://github.com/yugabyte/node-postgres) | Full | [CRUD](/preview/drivers-orms/nodejs/yugabyte-node-driver/) |
| PostgreSQL node-postgres Driver | [8.7.3](https://www.npmjs.com/package/pg) | Full | [CRUD](/preview/drivers-orms/nodejs/postgres-node-driver/) |
| YugabyteDB Node.js Driver for YCQL | [4.0.0](https://github.com/yugabyte/cassandra-nodejs-driver) | Full | [CRUD](/preview/drivers-orms/nodejs/ycql/) |
| **ORMs** | | | |
| Sequelize | 6.6.5 | Full | [CRUD](/preview/drivers-orms/nodejs/sequelize/) |
| Prisma    | 3.14  | Full | [CRUD](/preview/drivers-orms/nodejs/prisma/) |

  {{% /tab %}}

  {{% tab header="C" lang="c" %}}

| Driver        | Version | Support Level | Example apps |
| :------------ | :------ | :------------ | :----------- |
| libpq C Driver| 5.11    | Full          | [CRUD](/preview/drivers-orms/c/ysql/) |

  {{% /tab %}}

  {{% tab header="C++" lang="cpp" %}}

| Driver     | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| libpqxx C++ Driver             | | Full | [CRUD](/preview/drivers-orms/cpp/ysql/) |
| YugabyteDB C++ Driver for YCQL | [2.9.0-yb-10](https://github.com/yugabyte/cassandra-cpp-driver/releases) | Full | [CRUD](/preview/drivers-orms/cpp/ycql/) |

  {{% /tab %}}

  {{% tab header="C#" lang="csharp" %}}

|            | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| **Drivers** | | | |
| YugabyteDB C# Smart Driver for YSQL | [8.0.0-yb-1-beta](https://www.nuget.org/packages/NpgsqlYugabyteDB/) | Full | [CRUD](/preview/drivers-orms/csharp/ysql/) |
| PostgreSQL Npgsql Driver            | [6.0.3](https://www.nuget.org/packages/Npgsql/6.0.3) | Full | [CRUD](/preview/drivers-orms/csharp/postgres-npgsql/) |
| YugabyteDB C# Driver for YCQL       | [3.6.0](https://github.com/yugabyte/cassandra-csharp-driver/releases/tag/3.6.0) | Full | [CRUD](/preview/drivers-orms/csharp/ycql/) |
| **ORM** | | | |
| Entity Framework                    | 6.0.2  | Full | [CRUD](/preview/drivers-orms/csharp/entityframework/) |

  {{% /tab %}}

  {{% tab header="Ruby" lang="ruby" %}}

|             | Version | Support Level | Example apps |
| :---------- | :------ | :------------ | :----------- |
| **Drivers** | | | |
| Pg Gem Driver                   | [1.5.4](https://github.com/ged/ruby-pg) | Full | [CRUD](/preview/drivers-orms/ruby/ysql-pg/) |
| YugabyteDB Ruby Driver for YCQL | [3.2.3.2](https://github.com/yugabyte/cassandra-ruby-driver) | Full | [CRUD](/preview/drivers-orms/ruby/ycql/) |
| **ORM**     | | | |
| Active Record                   | 7.0.4 | Full | [CRUD](/preview/drivers-orms/ruby/activerecord/) |

  {{% /tab %}}

  {{% tab header="Rust" lang="rust" %}}

| ORM        | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| **Driver** | | | |
| Rust-postgres Driver | v0.19.7-yb-1-beta <br/> v0.7.10-yb-1-beta | Full | [CRUD](/preview/drivers-orms/rust/yb-rust-postgres/) |
| **ORM**    | | | |
| Diesel     | 1.42    | Full          | [CRUD](/preview/drivers-orms/rust/diesel/) |

  {{% /tab %}}

  {{% tab header="PHP" lang="php" %}}

|                  | Version | Support Level | Example apps |
| :--------------- | :------ | :------------ | :----------- |
| **Driver** | | | |
| php-pgsql Driver |         | Full | [CRUD](/preview/drivers-orms/php/ysql/) |
| **ORM**    | | | |
| Laravel          | 8.40    | Full | [CRUD](/preview/drivers-orms/php/laravel/) |

  {{% /tab %}}

  {{% tab header="Scala" lang="scala" %}}

| Driver     | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| YugabyteDB Java Driver for YCQL | [3.10.3-yb-2](https://mvnrepository.com/artifact/com.yugabyte/cassandra-driver-core/3.10.3-yb-2) | Full | [CRUD](/preview/drivers-orms/scala/ycql/) |

  {{% /tab %}}

{{< /tabpane >}}
