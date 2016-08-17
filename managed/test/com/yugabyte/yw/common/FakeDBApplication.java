// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Commissioner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;
import play.test.WithApplication;

import java.util.Map;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

public class FakeDBApplication extends WithApplication {

  @Override
  protected Application provideApplication() {
    ApiHelper mockApiHelper = mock(ApiHelper.class);
    Commissioner mockCommissioner = mock(Commissioner.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .build();
  }
}
