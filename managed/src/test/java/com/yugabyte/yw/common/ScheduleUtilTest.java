// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class ScheduleUtilTest extends FakeDBApplication {

  private Universe defaultUniverse;
  private Customer defaultCustomer;
  private CustomerConfig customerConfig;
  private BackupRequestParams backupRequestParams;
  private Users defaultUser;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultUser = ModelFactory.testUser(defaultCustomer);

    backupRequestParams = new BackupRequestParams();
    backupRequestParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST16");
    backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();
  }

  @Test
  public void testDetectIncrementalBackup() {
    // Set http context
    TestUtils.setFakeHttpContext(defaultUser);
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupRequestParams, TaskType.CreateBackup, 1000, null);
    assertFalse(ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID()));
    backupRequestParams.incrementalBackupFrequency = 100000L;
    schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupRequestParams, TaskType.CreateBackup, 1000, null);
    assertTrue(ScheduleUtil.isIncrementalBackupSchedule(schedule.getScheduleUUID()));
  }
}
