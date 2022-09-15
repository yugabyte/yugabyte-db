package org.yb.perf_advisor.common;

import io.ebean.CallableSql;
import io.ebean.EbeanServer;
import io.ebean.EbeanServerFactory;
import io.ebean.config.ServerConfig;
import io.ebean.datasource.DataSourceConfig;
import io.zonky.test.db.postgres.embedded.EmbeddedPostgres;
import org.flywaydb.core.Flyway;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeAll;

import javax.sql.DataSource;
import java.io.IOException;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Map;

public class MockServer {
  private static EmbeddedPostgres db;
  public static EbeanServer ebeanServer;

  @BeforeAll
  public static void setup() throws IOException, SQLException {
    setupDb();
  }

  public static void setupDb() throws IOException, SQLException {
    // Start embedded postgres
    db = EmbeddedPostgres.builder().start();
    DataSource postgresDatabase = db.getPostgresDatabase();

    ServerConfig sc = new ServerConfig();
    sc.setName("test-db");
    sc.setDefaultServer(true);
    DataSourceConfig dsc = new DataSourceConfig();
    dsc.setUrl(postgresDatabase.getConnection().getMetaData().getURL());
    dsc.setUsername(postgresDatabase.getConnection().getMetaData().getUserName());
    dsc.setPassword("");
    dsc.setDriver("org.postgresql.Driver");
    sc.setDataSourceConfig(dsc);
    ebeanServer = EbeanServerFactory.create(sc);

    // Run migrations
    Flyway flyway = new Flyway();
    Map<String, String> properties = new HashMap<>();
    properties.put("flyway.url", dsc.getUrl());
    properties.put("flyway.user", dsc.getUsername());
    properties.put("flyway.password", dsc.getPassword());
    flyway.configure(properties);
    flyway.setLocations("db/migration/perf_advisor");
    flyway.migrate();
  }

  @AfterEach
  public void resetDb(){
    // Reset the database
    CallableSql callableSql = ebeanServer.createCallableSql("do\n" +
      "$$\n" +
      "declare\n" +
      "  l_stmt text;\n" +
      "begin\n" +
      "  select 'truncate ' || string_agg(format('%I.%I', schemaname, tablename), ',')\n" +
      "    into l_stmt\n" +
      "  from pg_tables\n" +
      "  where schemaname in ('public');\n" +
      "\n" +
      "  execute l_stmt;\n" +
      "end;\n" +
      "$$");
    ebeanServer.execute(callableSql);
  }

  @AfterAll
  public static void closeDb() throws IOException {
    db.close();
  }
}
