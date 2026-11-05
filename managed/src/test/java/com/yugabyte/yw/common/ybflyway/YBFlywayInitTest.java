// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.ybflyway;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import java.io.File;
import java.util.Arrays;
import java.util.List;
import org.flywaydb.core.api.Location;
import org.flywaydb.core.api.configuration.FluentConfiguration;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.BlockJUnit4ClassRunner;
import play.Environment;
import play.Mode;

@RunWith(BlockJUnit4ClassRunner.class)
public class YBFlywayInitTest {

  // Merges YBA's reference.conf with yb-perf-advisor's, as a production deployment does.
  private static final Config REFERENCE = ConfigFactory.defaultReference();

  private final Environment environment =
      new Environment(new File("."), getClass().getClassLoader(), Mode.TEST);

  private List<FluentConfiguration> build(String overrides) {
    Config config = ConfigFactory.parseString(overrides).withFallback(REFERENCE).resolve();
    return YBFlywayInit.buildConfigurations(config, environment);
  }

  private static String[] locations(FluentConfiguration flyway) {
    return Arrays.stream(flyway.getLocations()).map(Location::getDescriptor).toArray(String[]::new);
  }

  @Test
  public void testReferenceConfig() {
    List<FluentConfiguration> flyways = build("");
    assertEquals(2, flyways.size());

    FluentConfiguration yba = flyways.get(0);
    assertArrayEquals(
        new String[] {
          "classpath:db/migration/default_/common", "classpath:db/migration/default_/postgres"
        },
        locations(yba));
    assertEquals("jdbc:postgresql://localhost:5432/yugaware", yba.getUrl());
    assertEquals("org.postgresql.Driver", yba.getDriver());
    assertEquals("postgres", yba.getUser());
    assertEquals("schema_version", yba.getTable());
    assertTrue(yba.isBaselineOnMigrate());
    assertTrue(yba.isOutOfOrder());
    assertEquals(2, yba.getIgnoreMigrationPatterns().length);

    FluentConfiguration perfAdvisor = flyways.get(1);
    assertArrayEquals(
        new String[] {"classpath:db/migration/perf_advisor/common"}, locations(perfAdvisor));
    assertEquals("jdbc:postgresql://localhost:5432/perf_advisor", perfAdvisor.getUrl());
    assertEquals("schema_version", perfAdvisor.getTable());
  }

  @Test
  public void testDisabledDbDoesNotSkipOthers() {
    // Sorts before "default"; skipping it must not stop the remaining databases from migrating.
    List<FluentConfiguration> flyways =
        build(
            "db.a_disabled { url=\"jdbc:hsqldb:mem:x\", driver=\"org.hsqldb.jdbc.JDBCDriver\","
                + " migration { auto=true, disabled=true, scriptsDirectory=\"default_\" } }\n"
                + "db.perf_advisor.migration.disabled=true");
    assertEquals(1, flyways.size());
    assertEquals("jdbc:postgresql://localhost:5432/yugaware", flyways.get(0).getUrl());
  }

  @Test
  public void testAutoFalseAndMissingDirectorySkipped() {
    List<FluentConfiguration> flyways =
        build(
            "db.default.migration.auto=false\n"
                + "db.perf_advisor.migration.scriptsDirectory=\"does_not_exist\"");
    assertEquals(0, flyways.size());
  }

  @Test
  public void testPostgresFullUrl() {
    FluentConfiguration yba =
        build(
                "db.default.url=\"postgres://yba:secret@pghost:5433/yugaware\"\n"
                    + "db.perf_advisor.migration.auto=false")
            .get(0);
    assertEquals("jdbc:postgresql://pghost:5433/yugaware", yba.getUrl());
    assertEquals("yba", yba.getUser());
    assertEquals("secret", yba.getPassword());
  }
}
