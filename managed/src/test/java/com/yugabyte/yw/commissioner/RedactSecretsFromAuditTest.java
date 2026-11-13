// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.CALLS_REAL_METHODS;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.verify;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.ebean.DB;
import java.time.Duration;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.MockedStatic;
import play.libs.Json;

public class RedactSecretsFromAuditTest extends FakeDBApplication {

  private Users user;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer("tc1", "Test Customer 1");
    user = ModelFactory.testUser(customer);
  }

  @Test
  public void testRedactsGFlagChangesInAdditionalDetails() {
    UUID plainTask = createAudit(gflagChange("ycql_ldap_bind_passwd", "oldSecret", "newSecret"));
    ObjectNode fallback =
        Json.newObject()
            .put("_redactionParseError", true)
            .put(
                "_redactedPayload",
                "{\"gflags\":{\"tserver\":[{\"name\":\"ycql_ldap_bind_passwd\",\"old\":\"secret\"");
    UUID fallbackTask = createAudit(fallback);
    ObjectNode otherChange = gflagChange("webserver_port", "9000", "9100");
    UUID otherTask = createAudit(otherChange);
    UUID noDetailsTask = createAudit(null);
    String hba = "host all all 0.0.0.0/0 ldap LDAP\"BIND\"PASSWD=";
    UUID hbaTask = createAudit(gflagChange("ysql_hba_conf_csv", hba + "old", hba + "new"));
    DB.sqlUpdate("UPDATE audit SET last_redacted_version = 0").execute();

    runRedaction();

    assertEquals(
        gflagChange("ycql_ldap_bind_passwd", SECRET_REPLACEMENT, SECRET_REPLACEMENT),
        Audit.getFromTaskUUID(plainTask).getAdditionalDetails());
    assertEquals(
        gflagChange("ysql_hba_conf_csv", hba + SECRET_REPLACEMENT, hba + SECRET_REPLACEMENT),
        Audit.getFromTaskUUID(hbaTask).getAdditionalDetails());
    assertEquals(
        SECRET_REPLACEMENT,
        Audit.getFromTaskUUID(fallbackTask)
            .getAdditionalDetails()
            .get("_redactedPayload")
            .asText());
    assertEquals(otherChange, Audit.getFromTaskUUID(otherTask).getAdditionalDetails());
    assertNull(Audit.getFromTaskUUID(noDetailsTask).getAdditionalDetails());
  }

  @Test
  public void testLeavesEntryUnmarkedWhenAdditionalDetailsFail() {
    ObjectNode failingChange = gflagChange("ycql_ldap_bind_passwd", "oldSecret", "newSecret");
    UUID failingTask = createAudit(failingChange);
    UUID otherTask = createAudit(gflagChange("ycql_ldap_bind_passwd", "secret1", "secret2"));
    DB.sqlUpdate("UPDATE audit SET last_redacted_version = 0").execute();

    try (MockedStatic<RedactingService> redacting =
        mockStatic(RedactingService.class, CALLS_REAL_METHODS)) {
      redacting
          .when(() -> RedactingService.filterSecretFields(failingChange, RedactionTarget.LOGS))
          .thenThrow(new RuntimeException("injected"));
      runRedaction();
    }

    assertEquals(0, Audit.getFromTaskUUID(failingTask).getLastRedactedVersion());
    assertNotEquals(0, Audit.getFromTaskUUID(otherTask).getLastRedactedVersion());
    assertEquals(
        gflagChange("ycql_ldap_bind_passwd", SECRET_REPLACEMENT, SECRET_REPLACEMENT),
        Audit.getFromTaskUUID(otherTask).getAdditionalDetails());
  }

  private void runRedaction() {
    PlatformScheduler scheduler = mock(PlatformScheduler.class);
    new RedactSecretsFromAudit(
            scheduler,
            app.injector().instanceOf(RedactingService.class),
            app.injector().instanceOf(RuntimeConfGetter.class))
        .start();
    ArgumentCaptor<Runnable> runner = ArgumentCaptor.forClass(Runnable.class);
    verify(scheduler)
        .schedule(anyString(), any(Duration.class), any(Duration.class), runner.capture());
    runner.getValue().run();
  }

  private UUID createAudit(JsonNode additionalDetails) {
    UUID taskUUID = UUID.randomUUID();
    Audit.create(
        user,
        "/api/test",
        "POST",
        Audit.TargetType.Universe,
        "target",
        Audit.ActionType.UpgradeGFlags,
        null,
        taskUUID,
        additionalDetails,
        null);
    return taskUUID;
  }

  private static ObjectNode gflagChange(String name, String oldValue, String newValue) {
    ObjectNode details = Json.newObject();
    details
        .putObject("gflags")
        .putArray("tserver")
        .addObject()
        .put("name", name)
        .put("old", oldValue)
        .put("new", newValue);
    return details;
  }
}
