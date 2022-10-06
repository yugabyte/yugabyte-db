// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
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

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());

    backupRequestParams = new BackupRequestParams();
    backupRequestParams.universeUUID = defaultUniverse.universeUUID;
    customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST16");
    backupRequestParams.storageConfigUUID = customerConfig.configUUID;
  }

  @Test
  public void testDetectIncrementalBackup() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.uuid, backupRequestParams, TaskType.CreateBackup, 1000, null);
    assertFalse(ScheduleUtil.isIncrementalBackupSchedule(schedule.scheduleUUID));
    backupRequestParams.incrementalBackupFrequency = 100000L;
    schedule =
        Schedule.create(
            defaultCustomer.uuid, backupRequestParams, TaskType.CreateBackup, 1000, null);
    assertTrue(ScheduleUtil.isIncrementalBackupSchedule(schedule.scheduleUUID));
  }
}
