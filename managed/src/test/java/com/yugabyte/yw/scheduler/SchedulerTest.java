// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.scheduler;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Universe;

import java.util.List;
import java.util.UUID;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Matchers;
import org.mockito.runners.MockitoJUnitRunner;
import org.mockito.Mock;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;
import scala.concurrent.duration.FiniteDuration;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class SchedulerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(SchedulerTest.class);

  private static Commissioner mockCommissioner;
  private CustomerConfig s3StorageConfig;
  @Mock
  private Scheduler mockScheduler;
  com.yugabyte.yw.scheduler.Scheduler scheduler;
  Customer defaultCustomer;
  ActorSystem mockActorSystem;
  ExecutionContext mockExecutionContext;

  @Before
  public void setUp() {
    mockActorSystem = mock(ActorSystem.class);
    mockExecutionContext = mock(ExecutionContext.class);
    mockCommissioner = mock(Commissioner.class);
    when(mockActorSystem.scheduler()).thenReturn(mockScheduler);
    scheduler = new com.yugabyte.yw.scheduler.Scheduler(mockActorSystem,
        mockExecutionContext, mockCommissioner);
  }

  @Test
  public void scheduleManualBackupWithExpiryTest() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(), Matchers.any())).thenReturn(fakeTaskUUID);

    defaultCustomer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer);

    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());

    Backup backup = ModelFactory.createBackupWithExpiry(defaultCustomer.uuid, universe.universeUUID,
        s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);

    scheduler.scheduleRunner();
 
    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
  }
}
