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
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigValue;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.util.concurrent.CompletableFuture;
import lombok.extern.slf4j.Slf4j;
import play.Environment;
import play.inject.ApplicationLifecycle;
import play.libs.ws.WSClient;
import play.libs.ws.ahc.AhcWSClient;
import play.libs.ws.ahc.AhcWSClientConfigFactory;

@Singleton
@Slf4j
public class CustomWsClientFactory {

  private final ApplicationLifecycle lifecycle;
  private final Materializer materializer;
  private final Environment environment;
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public CustomWsClientFactory(
      ApplicationLifecycle lifecycle,
      Materializer materializer,
      Environment environment,
      RuntimeConfigFactory runtimeConfigFactory) {
    this.lifecycle = lifecycle;
    this.materializer = materializer;
    this.environment = environment;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public WSClient forCustomConfig(ConfigValue wsOverrides) {
    Config customWsConfig =
        ConfigFactory.empty()
            .withValue("play.ws", wsOverrides)
            .withFallback(runtimeConfigFactory.staticApplicationConf())
            .withOnlyPath("play.ws");
    // Enable trace level logging to debug actual config value being resolved:
    log.trace("Creating ws client with config: {}", customWsConfig.root().render());
    AhcWSClient customWsClient =
        AhcWSClient.create(
            AhcWSClientConfigFactory.forConfig(customWsConfig, environment.classLoader()),
            null, // no HTTP caching
            materializer);
    lifecycle.addStopHook(
        () -> {
          customWsClient.close();
          return CompletableFuture.completedFuture(null);
        });
    return customWsClient;
  }
}
