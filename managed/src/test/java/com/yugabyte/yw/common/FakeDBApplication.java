// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
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
  public ApiHelper mockApiHelper;
  HealthChecker mockHealthChecker;
  protected EncryptionAtRestManager mockEARManager;

  @Override
  protected Application provideApplication() {
    mockEARManager = mock(EncryptionAtRestManager.class);
    mockApiHelper = mock(ApiHelper.class);
    mockCommissioner = mock(Commissioner.class);
    mockCallHome = mock(CallHome.class);
    Executors mockExecutors = mock(Executors.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(CallHome.class).toInstance(mockCallHome))
        .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
        .overrides(bind(Executors.class).toInstance(mockExecutors))
        .overrides(bind(EncryptionAtRestManager.class).toInstance(mockEARManager))
      .build();
  }
}
