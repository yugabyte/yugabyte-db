// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.yugabyte.yw.common.CallHomeManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import java.util.concurrent.TimeUnit;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.runners.MockitoJUnitRunner;
import play.Environment;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;
import scala.concurrent.duration.FiniteDuration;

@RunWith(MockitoJUnitRunner.class)
public class CallHomeTest extends FakeDBApplication {

  CallHome callHome;

  Environment mockEnvironment;
  ActorSystem mockActorSystem;
  ExecutionContext mockExecutionContext;
  CallHomeManager mockCallHomeManager;
  Scheduler mockScheduler;

  Customer defaultCustomer;

  @Before
  public void setUp() {
    mockEnvironment = mock(Environment.class);
    mockActorSystem = mock(ActorSystem.class);
    mockExecutionContext = mock(ExecutionContext.class);
    mockCallHomeManager = mock(CallHomeManager.class);
    mockScheduler = mock(Scheduler.class);
    when(mockActorSystem.scheduler()).thenReturn(mockScheduler);
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void scheduleRunnerSingleTenant() {
    callHome =
        new CallHome(mockActorSystem, mockExecutionContext, mockCallHomeManager, mockEnvironment);
    callHome.scheduleRunner();
    verify(mockCallHomeManager, times(1)).sendDiagnostics(defaultCustomer);
  }

  @Test
  public void scheduleRunnerMultiTenant() {
    Customer newCustomer = ModelFactory.testCustomer("tc2", "Test Customer 2");
    callHome =
        new CallHome(mockActorSystem, mockExecutionContext, mockCallHomeManager, mockEnvironment);
    callHome.scheduleRunner();
    verify(mockCallHomeManager, times(1)).sendDiagnostics(defaultCustomer);
    verify(mockCallHomeManager, times(1)).sendDiagnostics(newCustomer);
  }

  @Test
  public void testScheduleForDevEnvironment() {
    when(mockEnvironment.isDev()).thenReturn(true);
    callHome =
        new CallHome(mockActorSystem, mockExecutionContext, mockCallHomeManager, mockEnvironment);
    callHome.start();
    verify(mockActorSystem, times(0)).scheduler();
  }

  @Test
  public void testScheduleForNonDevEnvironment() {
    when(mockEnvironment.isDev()).thenReturn(false);
    callHome =
        new CallHome(mockActorSystem, mockExecutionContext, mockCallHomeManager, mockEnvironment);
    callHome.start();
    ArgumentCaptor<FiniteDuration> initialDelay = ArgumentCaptor.forClass(FiniteDuration.class);
    ArgumentCaptor<FiniteDuration> interval = ArgumentCaptor.forClass(FiniteDuration.class);
    ArgumentCaptor<Runnable> mockScheduleRunner = ArgumentCaptor.forClass(Runnable.class);
    ArgumentCaptor<ExecutionContext> expectedExceutionContext =
        ArgumentCaptor.forClass(ExecutionContext.class);

    verify(mockScheduler)
        .schedule(
            initialDelay.capture(),
            interval.capture(),
            mockScheduleRunner.capture(),
            expectedExceutionContext.capture());
    assertEquals(Duration.create(0, TimeUnit.MINUTES), initialDelay.getValue());
    assertEquals(Duration.create(60, TimeUnit.MINUTES), interval.getValue());
    assertNotNull(mockScheduleRunner.getValue());
    assertNotNull(expectedExceutionContext.getValue());
  }
}
