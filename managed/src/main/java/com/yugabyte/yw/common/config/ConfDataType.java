/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.typesafe.config.Config;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.function.BiFunction;
import java.util.function.Function;
import lombok.Getter;

@Getter
public class ConfDataType<T> {
  static ConfDataType<Duration> DurationType =
      new ConfDataType<>("Duration", Duration.class, Config::getDuration, Duration::parse);
  static ConfDataType<Double> DoubleType =
      new ConfDataType<>("Double", Double.class, Config::getDouble, Double::parseDouble);

  // TODO:
  //  String,
  //  Long,
  //  Boolean,
  //  Bytes,
  //  Period,
  //  Temporal,

  // Example for enum type
  static ConfDataType<TaskType> TaskTypeEnum =
      new ConfDataType<>(
          "TaskType", TaskType.class, new EnumGetter<>(TaskType.class), TaskType::valueOf);

  private final String name;

  @JsonIgnore private final Class<T> type;

  @JsonIgnore private final BiFunction<Config, String, T> getter;

  @JsonIgnore private final Function<String, T> parser;

  ConfDataType(
      String name,
      Class<T> type,
      BiFunction<Config, String, T> getter,
      Function<String, T> parser) {
    this.name = name;
    this.type = type;
    this.getter = getter;
    this.parser = parser;
  }

  static class EnumGetter<T extends Enum<T>> implements BiFunction<Config, String, T> {

    private final Class<T> enumClass;

    EnumGetter(Class<T> enumClass) {
      this.enumClass = enumClass;
    }

    @Override
    public T apply(Config config, String path) {
      return config.getEnum(enumClass, path);
    }
  }
}
