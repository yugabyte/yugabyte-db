// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;
import play.test.WithApplication;

import java.util.Map;
import java.util.concurrent.Executors;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

public class FakeDBApplication extends WithApplication {
  public Commissioner mockCommissioner;
  protected CallHome mockCallHome;

  @Override
  protected Application provideApplication() {
    ApiHelper mockApiHelper = mock(ApiHelper.class);
    mockCommissioner = mock(Commissioner.class);
    mockCallHome = mock(CallHome.class);
    Executors mockExecutors = mock(Executors.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(CallHome.class).toInstance(mockCallHome))
        .overrides(bind(Executors.class).toInstance(mockExecutors))
      .build();
  }
}
