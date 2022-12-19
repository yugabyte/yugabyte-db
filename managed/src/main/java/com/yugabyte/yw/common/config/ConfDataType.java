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

import static play.mvc.Http.Status.BAD_REQUEST;

import java.time.Duration;
import java.time.Period;
import java.util.List;
import java.util.Set;
import java.util.function.BiFunction;
import java.util.function.Function;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.core.type.TypeReference;
import com.google.common.collect.ImmutableSet;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.VersionCheckMode;
import com.yugabyte.yw.common.PlatformServiceException;

import lombok.Getter;
import play.libs.Json;

@Getter
public class ConfDataType<T> {
  static ConfDataType<Duration> DurationType =
      new ConfDataType<>("Duration", Duration.class, Config::getDuration, Duration::parse);
  static ConfDataType<Double> DoubleType =
      new ConfDataType<>("Double", Double.class, Config::getDouble, Double::parseDouble);
  static ConfDataType<String> StringType =
      new ConfDataType<>("String", String.class, Config::getString, String::valueOf);
  static ConfDataType<Long> LongType =
      new ConfDataType<>("Long", Long.class, Config::getLong, Long::parseLong);
  static ConfDataType<Boolean> BooleanType =
      new ConfDataType<>("Boolean", Boolean.class, Config::getBoolean, ConfDataType::parseBoolean);
  static ConfDataType<Period> PeriodType =
      new ConfDataType<>("Period", Period.class, Config::getPeriod, Period::parse);
  static ConfDataType<Integer> IntegerType =
      new ConfDataType<>("Integer", Integer.class, Config::getInt, Integer::parseInt);
  static ConfDataType<List> StringListType =
      new ConfDataType<>(
          "String List", List.class, Config::getStringList, ConfDataType::parseStrList);
  static ConfDataType<Long> BytesType =
      new ConfDataType<>(
          "Bytes",
          Long.class,
          Config::getBytes,
          (s) -> {
            Config c = ConfigFactory.parseString("bytes = " + s);
            return c.getBytes("bytes");
          });

  static ConfDataType<VersionCheckMode> VersionCheckModeEnum =
      new ConfDataType<>(
          "VersionCheckMode",
          VersionCheckMode.class,
          new EnumGetter<>(VersionCheckMode.class),
          VersionCheckMode::valueOf);

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

  static final Set<String> BOOLS = ImmutableSet.of("true", "false");

  public static Boolean parseBoolean(String s) {
    if (BOOLS.contains(s.toLowerCase().trim())) {
      return Boolean.parseBoolean(s.trim());
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Not a valid boolean value");
    }
  }

  public static List<String> parseStrList(String s) {
    try {
      List<String> strList = Json.mapper().readValue(s, new TypeReference<List<String>>() {});
      return strList;
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, "Not a valid list of strings");
    }
  }
}
