// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.ybflyway;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigUtil;
import com.typesafe.config.ConfigValueType;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.Flyway;
import org.flywaydb.core.api.configuration.FluentConfiguration;
import play.Environment;

/**
 * Runs Flyway migrations at startup for every {@code db.<name>} datasource that has a {@code
 * migration} block. Reads the same config keys the flyway-play library did, so existing configs
 * (reference.conf, yba-installer, Helm, and yb-perf-advisor's reference.conf) keep working.
 */
@Slf4j
@Singleton
public class YBFlywayInit {

  private static final String MIGRATION_ROOT = "db/migration";

  // Play accepts postgres://user:pass@host/db in db.<name>.url, so keep accepting it here.
  private static final Pattern POSTGRES_FULL_URL =
      Pattern.compile("^postgres://([a-zA-Z0-9_]+):([^@]+)@([^/]+)/([^\\s]+)$");

  @Inject
  public YBFlywayInit(Config config, Environment environment) {
    for (FluentConfiguration flywayConfig : buildConfigurations(config, environment)) {
      try {
        flywayConfig.load().migrate();
      } catch (Exception e) {
        log.error("migration failed: ", e);
        throw e;
      }
    }
  }

  static List<FluentConfiguration> buildConfigurations(Config config, Environment environment) {
    List<FluentConfiguration> result = new ArrayList<>();
    if (!config.hasPath("db")) {
      return result;
    }
    // Sorted so that "default" (YBA's own schema) migrates before "perf_advisor".
    for (String dbName : new TreeSet<>(config.getObject("db").keySet())) {
      if (config.getObject("db").get(dbName).valueType() != ConfigValueType.OBJECT) {
        continue;
      }
      Config db = config.getConfig(ConfigUtil.joinPath("db", dbName));
      Config migration =
          db.hasPath("migration") ? db.getConfig("migration") : ConfigFactory.empty();
      if (getBoolean(migration, "disabled", false) || !getBoolean(migration, "auto", false)) {
        continue;
      }
      if (!db.hasPath("url") || !db.hasPath("driver")) {
        log.warn("Skipping migrations for db.{}: url or driver is not set", dbName);
        continue;
      }
      String scriptsDirectory =
          MIGRATION_ROOT
              + "/"
              + (migration.hasPath("scriptsDirectory")
                  ? migration.getString("scriptsDirectory")
                  : dbName);
      if (environment.resource(scriptsDirectory) == null) {
        log.warn("Directory for migration files not found. {}", scriptsDirectory);
        continue;
      }
      result.add(buildConfiguration(db, migration, scriptsDirectory, environment));
    }
    return result;
  }

  private static FluentConfiguration buildConfiguration(
      Config db, Config migration, String scriptsDirectory, Environment environment) {
    String url = db.getString("url");
    String user = getString(db, "username", getString(db, "user", null));
    String password = getString(db, "password", getString(db, "pass", null));
    Matcher m = POSTGRES_FULL_URL.matcher(url);
    if (m.matches()) {
      url = String.format("jdbc:postgresql://%s/%s", m.group(3), m.group(4));
      user = m.group(1);
      password = m.group(2);
    }

    FluentConfiguration flyway =
        Flyway.configure(environment.classLoader())
            .driver(db.getString("driver"))
            .dataSource(url, user, password);
    List<String> locations =
        migration.hasPath("locations") ? migration.getStringList("locations") : List.of();
    if (locations.isEmpty()) {
      flyway.locations(scriptsDirectory);
    } else {
      flyway.locations(
          locations.stream().map(l -> scriptsDirectory + "/" + l).toArray(String[]::new));
    }
    if (migration.hasPath("table")) {
      flyway.table(migration.getString("table"));
    }
    if (migration.hasPath("schemas")) {
      flyway.schemas(migration.getStringList("schemas").toArray(String[]::new));
    }
    if (migration.hasPath("encoding")) {
      flyway.encoding(migration.getString("encoding"));
    }
    if (migration.hasPath("ignoreMigrationPatterns")) {
      flyway.ignoreMigrationPatterns(
          migration.getStringList("ignoreMigrationPatterns").toArray(String[]::new));
    }
    if (migration.hasPath("initOnMigrate")) {
      flyway.baselineOnMigrate(migration.getBoolean("initOnMigrate"));
    }
    if (migration.hasPath("outOfOrder")) {
      flyway.outOfOrder(migration.getBoolean("outOfOrder"));
    }
    if (migration.hasPath("validateOnMigrate")) {
      flyway.validateOnMigrate(migration.getBoolean("validateOnMigrate"));
    }
    if (migration.hasPath("cleanDisabled")) {
      flyway.cleanDisabled(migration.getBoolean("cleanDisabled"));
    }
    if (migration.hasPath("mixed")) {
      flyway.mixed(migration.getBoolean("mixed"));
    }
    if (migration.hasPath("group")) {
      flyway.group(migration.getBoolean("group"));
    }
    return flyway;
  }

  private static boolean getBoolean(Config config, String path, boolean defaultValue) {
    return config.hasPath(path) ? config.getBoolean(path) : defaultValue;
  }

  private static String getString(Config config, String path, String defaultValue) {
    return config.hasPath(path) ? config.getString(path) : defaultValue;
  }
}
