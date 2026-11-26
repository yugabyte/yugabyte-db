// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.backuprestore;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.ArrayList;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes.TableType;

public class BackupScheduleTaskParamsTest extends FakeDBApplication {

  private Customer customer;
  private UUID universeUUID;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universeUUID = UUID.randomUUID();
  }

  private BackupRequestParams scheduleParams(String name) {
    BackupRequestParams params = new BackupRequestParams();
    params.scheduleName = name;
    params.setUniverseUUID(universeUUID);
    params.storageConfigUUID = UUID.randomUUID();
    params.backupType = TableType.PGSQL_TABLE_TYPE;
    params.keyspaceTableList = new ArrayList<>();
    return params;
  }

  private Schedule schedule(String name, Schedule.State state) {
    return Schedule.create(
        customer.getUuid(),
        universeUUID,
        scheduleParams(name),
        TaskType.CreateBackup,
        86_400_000L,
        null,
        true,
        TimeUnit.DAYS,
        name,
        state);
  }

  private BackupScheduleTaskParams taskParams(String name, UUID createdScheduleUUID) {
    BackupScheduleTaskParams params = new BackupScheduleTaskParams();
    params.setCustomerUUID(customer.getUuid());
    params.setScheduleParams(scheduleParams(name));
    params.createdScheduleUUID = createdScheduleUUID;
    return params;
  }

  private void validateRetry(BackupScheduleTaskParams params) {
    // Not the first try, so only the name is checked.
    params.validateCreate(false, null, null, null);
  }

  @Test
  public void theRequestsOwnScheduleIsNoConflict() {
    Schedule created = schedule("daily", Schedule.State.Creating);
    validateRetry(taskParams("daily", created.getScheduleUUID()));
    assertEquals(
        Schedule.State.Creating, Schedule.getOrBadRequest(created.getScheduleUUID()).getStatus());
  }

  @Test
  public void anotherScheduleWithTheNameFailsAndRemovesTheRequestsSchedule() {
    schedule("daily", Schedule.State.Active);
    Schedule created =
        Schedule.create(
            customer.getUuid(),
            UUID.randomUUID(),
            scheduleParams("daily"),
            TaskType.CreateBackup,
            86_400_000L,
            null,
            true,
            TimeUnit.DAYS,
            "daily",
            Schedule.State.Creating);

    assertThrows(
        PlatformServiceException.class,
        () -> validateRetry(taskParams("daily", created.getScheduleUUID())));
    assertFalse(Schedule.maybeGet(created.getScheduleUUID()).isPresent());
  }

  @Test
  public void aRetryAfterTheScheduleWasRemovedSaysSo() {
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> validateRetry(taskParams("daily", UUID.randomUUID())));
    assertTrue(e.getMessage(), e.getMessage().contains("create it again"));
  }

  @Test
  public void v1KeepsItsChecks() {
    schedule("daily", Schedule.State.Active);
    // A retry may reuse only a schedule in Error.
    assertThrows(PlatformServiceException.class, () -> validateRetry(taskParams("daily", null)));
    schedule("failed", Schedule.State.Error);
    validateRetry(taskParams("failed", null));
  }
}
