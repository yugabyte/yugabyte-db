/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import akka.stream.Materializer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.util.concurrent.CompletableFuture;
import play.Environment;
import play.inject.ApplicationLifecycle;
import play.libs.ws.WSClient;
import play.libs.ws.ahc.AhcWSClient;
import play.libs.ws.ahc.AhcWSClientConfigFactory;

@Singleton
public class CustomWsClientFactory {

  private final ApplicationLifecycle lifecycle;
  private final Materializer materializer;
  private final Environment environment;

  @Inject
  public CustomWsClientFactory(
      ApplicationLifecycle lifecycle, Materializer materializer, Environment environment) {
    this.lifecycle = lifecycle;
    this.materializer = materializer;
    this.environment = environment;
  }

  public WSClient forCustomConfig(Config customConfig) {
    AhcWSClient customeWsClient =
        AhcWSClient.create(
            AhcWSClientConfigFactory.forConfig(customConfig, environment.classLoader()),
            null, // no HTTP caching
            materializer);
    lifecycle.addStopHook(
        () -> {
          customeWsClient.close();
          return CompletableFuture.completedFuture(null);
        });
    return customeWsClient;
  }
}
