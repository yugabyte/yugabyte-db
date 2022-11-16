// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.models.Customer;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

public class SubTaskBaseTest extends PlatformGuiceApplicationBaseTest {
  Customer defaultCustomer;
  Commissioner mockCommissioner;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);

    return new GuiceApplicationBuilder()
        .disable(GuiceModule.class)
        .configure(testDatabase())
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .build();
  }
}
