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

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.AbstractModule;
import com.google.inject.Inject;
import com.google.inject.Provider;
import java.util.concurrent.CompletableFuture;
import play.inject.ApplicationLifecycle;
import play.libs.Json;

public class CustomObjectMapperModule extends AbstractModule {

  @Override
  protected void configure() {
    bind(ObjectMapper.class).toProvider(CustomObjectMapperProvider.class).asEagerSingleton();
  }

  static class CustomObjectMapperProvider implements Provider<ObjectMapper> {
    private final ApplicationLifecycle lifecycle;

    @Inject
    public CustomObjectMapperProvider(ApplicationLifecycle lifecycle) {
      this.lifecycle = lifecycle;
    }

    @Override
    public ObjectMapper get() {
      ObjectMapper mapper = Json.newDefaultMapper();
      mapper.setSerializationInclusion(Include.NON_NULL);
      Json.setObjectMapper(mapper);
      lifecycle.addStopHook(() -> CompletableFuture.runAsync(() -> Json.setObjectMapper(null)));
      return mapper;
    }
  }
}
