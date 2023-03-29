/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.modules;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jdk8.Jdk8Module;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
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
      Json.setObjectMapper(createDefaultMapper());
      lifecycle.addStopHook(() -> CompletableFuture.runAsync(() -> Json.setObjectMapper(null)));
      return Json.mapper();
    }
  }

  public static ObjectMapper createDefaultMapper() {
    ObjectMapper mapper = new ObjectMapper();
    mapper.registerModule(new Jdk8Module());
    mapper.registerModule(new JavaTimeModule());
    mapper.configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false);
    mapper.setSerializationInclusion(Include.NON_NULL);
    return mapper;
  }
}
