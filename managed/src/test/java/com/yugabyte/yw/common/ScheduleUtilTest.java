// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static play.test.Helpers.contextComponents;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.Users;

import java.util.Collections;
import java.util.Map;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.mvc.Http;

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
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    defaultUser = ModelFactory.testUser(defaultCustomer);

    backupRequestParams = new BackupRequestParams();
    backupRequestParams.universeUUID = defaultUniverse.universeUUID;
    customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST16");
    backupRequestParams.storageConfigUUID = customerConfig.configUUID;
  }

  @Test
  public void testDetectIncrementalBackup() {
    // Set http context
    Map<String, String> flashData = Collections.emptyMap();
    defaultUser.email = "shagarwal@yugabyte.com";
    Map<String, Object> argData =
        ImmutableMap.of("user", new UserWithFeatures().setUser(defaultUser));
    Http.Request request = mock(Http.Request.class);
    Long id = 2L;
    play.api.mvc.RequestHeader header = mock(play.api.mvc.RequestHeader.class);
    Http.Context currentContext =
        new Http.Context(id, header, request, flashData, flashData, argData, contextComponents());
    Http.Context.current.set(currentContext);

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
