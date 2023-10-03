<!--
+++
private = true
+++
-->

{{< tabpane text=true >}}

  {{% tab header="Java" lang="java" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| YugabyteDB JDBC Smart Driver [Recommended] | [42.3.5-yb-1](https://mvnrepository.com/artifact/com.yugabyte/jdbc-yugabytedb/42.3.5-yb-1) | Full | [CRUD](/preview/drivers-orms/java/yugabyte-jdbc/) |
| PostgreSQL JDBC Driver  | [42.3.4](https://mvnrepository.com/artifact/org.postgresql/postgresql/42.3.4) | Full | [CRUD](/preview/drivers-orms/java/postgres-jdbc/) |
| Vert.x Pg Client       | [4.3.2](https://mvnrepository.com/artifact/io.vertx/vertx-core/4.3.2) | Full | [CRUD](/preview/drivers-orms/java/ysql-vertx-pg-client/) |
| YugabyteDB Java Driver for YCQL | [3.10.3-yb-2](https://mvnrepository.com/artifact/com.yugabyte/cassandra-driver-core/3.10.3-yb-2) | Full | [CRUD](/preview/drivers-orms/java/ycql) |
| YugabyteDB Java Driver for YCQL | [4.15.0-yb-1](https://mvnrepository.com/artifact/com.yugabyte/java-driver-core/4.15.0-yb-1) | Full | [CRUD](/preview/drivers-orms/java/ycql-4.x) |
| Ebean                   | | Full | [CRUD](/preview/drivers-orms/java/ebean/) |
| Hibernate               | | Full | [CRUD](/preview/drivers-orms/java/hibernate/) |
| Spring Data YugabyteDB  | | Full | [CRUD](/preview/integrations/spring-framework/sdyb/#examples) |
| Spring Data JPA         | | Full | [CRUD](/preview/integrations/spring-framework/sd-jpa/#fundamentals) |
| MyBatis                 | | Full | [CRUD](/preview/drivers-orms/java/mybatis/) |
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->

  {{% /tab %}}

  {{% tab header="Go" lang="go" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| YugabyteDB PGX Smart Driver [Recommended] | [v4](https://pkg.go.dev/github.com/yugabyte/pgx/) | Full | [CRUD](/preview/drivers-orms/go/yb-pgx/) |
| PGX Driver | [v4](https://pkg.go.dev/github.com/jackc/pgx/) | Full | [CRUD](/preview/drivers-orms/go/pgx/) |
| PQ Driver  | [v1.10.2](https://github.com/lib/pq/releases/tag/v1.10.2/) | Full | [CRUD](/preview/drivers-orms/go/pq/) |
| YugabyteDB Go Driver for YCQL | [3.16.3](https://github.com/yugabyte/gocql) | Full | [CRUD](/preview/drivers-orms/go/ycql) |
| GORM       | | Full | [CRUD](/preview/drivers-orms/go/gorm/) |
| PG         | | Full | [CRUD](/preview/drivers-orms/go/pg/) |

  {{% /tab %}}

  {{% tab header="Python" lang="python" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| YugabyteDB Psycopg2 Smart Driver [Recommended] | [2.9.3](https://github.com/yugabyte/psycopg2) |Full | [CRUD](/preview/drivers-orms/python/yugabyte-psycopg2/) |
| PostgreSQL Psycopg2 Driver | [2.9.3](https://github.com/psycopg/psycopg2) | Full | [CRUD](/preview/drivers-orms/python/postgres-psycopg2/) |
| aiopg      | | Full | [Hello World](/preview/drivers-orms/python/aiopg/) |
| Django     | | Full | [CRUD](/preview/drivers-orms/python/django/) |
| SQLAlchemy | | Full | [CRUD](/preview/drivers-orms/python/sqlalchemy/) |

  {{% /tab %}}

  {{% tab header="NodeJS" lang="nodejs" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| YugabyteDB node-postgres Smart Driver [Recommended] | [8.7.3-yb-1](https://github.com/yugabyte/node-postgres) | Full | [CRUD](/preview/drivers-orms/nodejs/yugabyte-node-driver/) |
| PostgreSQL node-postgres Driver | [8.7.3](https://www.npmjs.com/package/pg) | Full | [CRUD](/preview/drivers-orms/nodejs/postgres-node-driver/) |
| Sequelize | | Full | [CRUD](/preview/drivers-orms/nodejs/sequelize/) |
| Prisma    | | Full | [CRUD](/preview/drivers-orms/nodejs/prisma/) |

  {{% /tab %}}

  {{% tab header="C" lang="c" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| libpq C Driver| Full | [CRUD](/preview/drivers-orms/c/ysql/) |

  {{% /tab %}}

  {{% tab header="C++" lang="cpp" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| libpqxx C++ Driver             | | Full | [CRUD](/preview/drivers-orms/cpp/ysql/) |
| YugabyteDB C++ Driver for YCQL | [2.9.0-yb-10](https://github.com/yugabyte/cassandra-cpp-driver/releases) | Full | [CRUD](/preview/drivers-orms/cpp/ycql/) |

  {{% /tab %}}

  {{% tab header="C#" lang="csharp" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| YugabyteDB C# Smart Driver for YSQL | [8.0.0-yb-1-beta](https://www.nuget.org/packages/NpgsqlYugabyteDB/) | Full | [CRUD](/preview/drivers-orms/csharp/ysql/) |
| PostgreSQL Npgsql Driver            | [6.0.3](https://www.nuget.org/packages/Npgsql/6.0.3) | Full | [CRUD](/preview/drivers-orms/csharp/postgres-npgsql/) |
| YugabyteDB C# Driver for YCQL       | [3.6.0](https://github.com/yugabyte/cassandra-csharp-driver/releases/tag/3.6.0) | Full | [CRUD](/preview/drivers-orms/csharp/ycql/) |
| Entity Framework                    | | Full | [CRUD](/preview/drivers-orms/csharp/entityframework/) |

  {{% /tab %}}

  {{% tab header="Ruby" lang="ruby" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| Pg Gem Driver                   | [1.5.4](https://github.com/ged/ruby-pg) | Full | [CRUD](/preview/drivers-orms/ruby/ysql-pg/) |
| YugabyteDB Ruby Driver for YCQL | [3.2.3.2](https://github.com/yugabyte/cassandra-ruby-driver) | Full | [CRUD](/preview/drivers-orms/ruby/ycql/) |
| ActiveRecord ORM                | | Full | [CRUD](/preview/drivers-orms/ruby/activerecord/) |

  {{% /tab %}}

  {{% tab header="Rust" lang="rust" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| Diesel     | Full | [CRUD](/preview/drivers-orms/rust/diesel/) |

  {{% /tab %}}

  {{% tab header="PHP" lang="php" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| php-pgsql Driver | Full | [CRUD](/preview/drivers-orms/php/ysql/) |
| Laravel          | Full | [CRUD](/preview/drivers-orms/php/laravel/) |

  {{% /tab %}}

  {{% tab header="Scala" lang="scala" %}}

| Driver/ORM | Version | Support Level | Example apps |
| :--------- | :------ | :------------ | :----------- |
| YugabyteDB Java Driver for YCQL | [3.10.3-yb-2](https://mvnrepository.com/artifact/com.yugabyte/cassandra-driver-core/3.10.3-yb-2) | Full | [CRUD](/preview/drivers-orms/scala/ycql/) |

  {{% /tab %}}

{{< /tabpane >}}
