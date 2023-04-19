// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.models.Customer;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

public class NodeTaskBaseTest extends PlatformGuiceApplicationBaseTest {
  NodeManager mockNodeManager;
  Customer defaultCustomer;
  Commissioner mockCommissioner;
  protected CallbackController mockCallbackController;
  protected PlayCacheSessionStore mockSessionStore;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Override
  protected Application provideApplication() {
    mockNodeManager = mock(NodeManager.class);
    mockCommissioner = mock(Commissioner.class);
    mockCallbackController = mock(CallbackController.class);
    mockSessionStore = mock(PlayCacheSessionStore.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);

    return new GuiceApplicationBuilder()
        .disable(GuiceModule.class)
        .configure(testDatabase())
        .overrides(bind(NodeManager.class).toInstance(mockNodeManager))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
        .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .build();
  }
}
