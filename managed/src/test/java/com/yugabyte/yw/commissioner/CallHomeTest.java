// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.CallHomeManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.models.Customer;
import java.time.Duration;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import play.Environment;

@RunWith(MockitoJUnitRunner.class)
public class CallHomeTest extends FakeDBApplication {

  CallHome callHome;

  Environment mockEnvironment;
  PlatformScheduler mockPlatformScheduler;
  CallHomeManager mockCallHomeManager;

  Customer defaultCustomer;

  @Before
  public void setUp() {
    mockEnvironment = mock(Environment.class);
    mockPlatformScheduler = mock(PlatformScheduler.class);
    mockCallHomeManager = mock(CallHomeManager.class);
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void scheduleRunnerSingleTenant() {
    callHome = new CallHome(mockPlatformScheduler, mockCallHomeManager, mockEnvironment);
    callHome.scheduleRunner();
    verify(mockCallHomeManager, times(1)).sendDiagnostics(defaultCustomer);
  }

  @Test
  public void scheduleRunnerMultiTenant() {
    Customer newCustomer = ModelFactory.testCustomer("tc2", "Test Customer 2");
    callHome = new CallHome(mockPlatformScheduler, mockCallHomeManager, mockEnvironment);
    callHome.scheduleRunner();
    verify(mockCallHomeManager, times(1)).sendDiagnostics(defaultCustomer);
    verify(mockCallHomeManager, times(1)).sendDiagnostics(newCustomer);
  }

  @Test
  public void testScheduleForDevEnvironment() {
    when(mockEnvironment.isDev()).thenReturn(true);
    callHome = new CallHome(mockPlatformScheduler, mockCallHomeManager, mockEnvironment);
    callHome.start();
    verify(mockPlatformScheduler, times(0)).schedule(any(), any(), any(), any());
  }

  @Test
  public void testScheduleForNonDevEnvironment() {
    when(mockEnvironment.isDev()).thenReturn(false);
    callHome = new CallHome(mockPlatformScheduler, mockCallHomeManager, mockEnvironment);
    callHome.start();
    ArgumentCaptor<String> name = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Duration> initialDelay = ArgumentCaptor.forClass(Duration.class);
    ArgumentCaptor<Duration> interval = ArgumentCaptor.forClass(Duration.class);
    ArgumentCaptor<Runnable> mockScheduleRunner = ArgumentCaptor.forClass(Runnable.class);

    verify(mockPlatformScheduler)
        .schedule(
            name.capture(),
            initialDelay.capture(),
            interval.capture(),
            mockScheduleRunner.capture());
    assertEquals(CallHome.class.getSimpleName(), name.getValue());
    assertEquals(Duration.ZERO, initialDelay.getValue());
    assertEquals(Duration.ofMinutes(60), interval.getValue());
    assertNotNull(mockScheduleRunner.getValue());
  }
}
