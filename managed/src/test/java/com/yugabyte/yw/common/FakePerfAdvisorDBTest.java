// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import static org.yb.perf_advisor.module.PerfAdvisorDB.DATABASE_NAME_PARAM;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.ebean.DB;
import io.ebean.Database;
import io.zonky.test.db.postgres.embedded.EmbeddedPostgres;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import javax.sql.DataSource;
import org.junit.Before;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.PerformanceRecommendation.EntityType;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationPriority;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationState;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationType;
import org.yb.perf_advisor.services.db.PerformanceRecommendationService;
import play.Application;

public class FakePerfAdvisorDBTest extends FakeDBApplication {

  private static EmbeddedPostgres db;

  protected Customer customer;

  protected Universe universe;

  protected PerformanceRecommendationService performanceRecommendationService;

  protected Database perfAdvisorEbeanServer;

  {
    try {
      db = EmbeddedPostgres.builder().start();
      Thread shutdownHook =
          new Thread("Stop postgres DB") {
            @Override
            public void run() {
              try {
                db.close();
              } catch (IOException e) {
                throw new RuntimeException(e);
              }
            }
          };
      Runtime.getRuntime().addShutdownHook(shutdownHook);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @Before
  public void baseSetup() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse();
    performanceRecommendationService =
        app.injector().instanceOf(PerformanceRecommendationService.class);
    perfAdvisorEbeanServer = DB.byName(app.config().getString(DATABASE_NAME_PARAM));
  }

  private static final Map<String, Object> PERF_ADVISOR_DB_PROPERTIES =
      ImmutableMap.<String, Object>builder()
          .put("db.perf_advisor.driver", "org.postgresql.Driver")
          .put("db.perf_advisor.migration.auto", true)
          .build();

  @Override
  protected Application provideApplication() {
    Map<String, Object> additionalConfiguration = new HashMap<>();
    try {
      DataSource postgresDatabase = db.getPostgresDatabase();
      additionalConfiguration.put(
          "db.perf_advisor.url", postgresDatabase.getConnection().getMetaData().getURL());
      additionalConfiguration.put(
          "db.perf_advisor.username", postgresDatabase.getConnection().getMetaData().getUserName());
      additionalConfiguration.putAll(PERF_ADVISOR_DB_PROPERTIES);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return provideApplication(additionalConfiguration);
  }

  protected PerformanceRecommendation createTestRecommendation() {
    Map<String, Object> recInfo = new HashMap<>();
    recInfo.put("sample-key", "sample-value");
    return new PerformanceRecommendation()
        .setRecommendationType(RecommendationType.CPU_SKEW)
        .setRecommendation("recommendation")
        .setObservation("observation")
        .setCustomerId(customer.getUuid())
        .setUniverseId(universe.getUniverseUUID())
        .setEntityType(EntityType.NODE)
        .setEntityNames("node-1, node-2")
        .setRecommendationInfo(recInfo)
        .setRecommendationState(RecommendationState.OPEN)
        .setRecommendationPriority(RecommendationPriority.MEDIUM);
  }
}
