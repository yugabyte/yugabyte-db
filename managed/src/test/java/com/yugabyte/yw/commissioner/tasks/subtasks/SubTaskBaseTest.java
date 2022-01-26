// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import org.junit.Before;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.WithApplication;

import java.util.Map;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

public class SubTaskBaseTest extends WithApplication {
  Customer defaultCustomer;
  Commissioner mockCommissioner;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);

    return new GuiceApplicationBuilder()
        .configure(testDatabase())
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .build();
  }
}
