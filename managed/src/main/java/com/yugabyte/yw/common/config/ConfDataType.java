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

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.core.type.TypeReference;
import com.google.common.collect.ImmutableSetMultimap;
import com.google.common.collect.SetMultimap;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.VersionCheckMode;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.LdapUtil.TlsProtocol;
import com.yugabyte.yw.common.NodeManager.SkipCertValidationType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import com.yugabyte.yw.models.Users.Role;
import java.time.Duration;
import java.time.Period;
import java.util.List;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Getter;
import org.apache.commons.lang3.StringUtils;
import org.apache.directory.api.ldap.model.message.SearchScope;
import play.libs.Json;

@Getter
public class ConfDataType<T> {
  static ConfDataType<Duration> DurationType =
      new ConfDataType<>(
          "Duration",
          Duration.class,
          Config::getDuration,
          (s) -> {
            return parseStringAndApply(s, Config::getDuration);
          });
  static ConfDataType<Double> DoubleType =
      new ConfDataType<>(
          "Double",
          Double.class,
          Config::getDouble,
          (s) -> {
            return parseStringAndApply(s, Config::getDouble);
          });
  static ConfDataType<String> StringType =
      new ConfDataType<>(
          "String",
          String.class,
          Config::getString,
          (s) -> {
            return parseString(s);
          });
  static ConfDataType<Long> LongType =
      new ConfDataType<>(
          "Long",
          Long.class,
          Config::getLong,
          (s) -> {
            return parseStringAndApply(s, Config::getLong);
          });
  static ConfDataType<Boolean> BooleanType =
      new ConfDataType<>(
          "Boolean",
          Boolean.class,
          Config::getBoolean,
          (s) -> {
            return parseStringAndApply(s, Config::getBoolean);
          });
  static ConfDataType<Period> PeriodType =
      new ConfDataType<>(
          "Period",
          Period.class,
          Config::getPeriod,
          (s) -> {
            return parseStringAndApply(s, Config::getPeriod);
          });
  static ConfDataType<Integer> IntegerType =
      new ConfDataType<>(
          "Integer",
          Integer.class,
          Config::getInt,
          (s) -> {
            return parseStringAndApply(s, Config::getInt);
          });
  static ConfDataType<List> StringListType =
      new ConfDataType<>(
          "String List",
          List.class,
          Config::getStringList,
          (s) -> {
            return parseStringAndApply(s, Config::getStringList);
          });
  static ConfDataType<List> IntegerListType =
      new ConfDataType<List>(
          "Integer List",
          List.class,
          (config, path) -> {
            return config.getIntList(path);
          },
          (s) -> {
            try {
              List<Integer> list =
                  Json.mapper().readValue(s, new TypeReference<List<Integer>>() {});
              return list;
            } catch (Exception e) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Not a valid list of integers. " + e.getMessage());
            }
          });
  static ConfDataType<Long> BytesType =
      new ConfDataType<>(
          "Bytes",
          Long.class,
          Config::getBytes,
          (s) -> {
            return parseStringAndApply(s, Config::getBytes);
          });
  static ConfDataType<List> TagListType =
      new ConfDataType<List>(
          "Tags List",
          List.class,
          (config, path) ->
              config.getStringList(path).stream()
                  .map(s -> ConfKeyTags.valueOf(s))
                  .collect(Collectors.toList()),
          ConfDataType::parseTagsList);

  static ConfDataType<SetMultimap> KeyValuesSetMultimapType =
      new ConfDataType<>(
          "Key Value SetMultimap",
          SetMultimap.class,
          (config, path) -> getSetMultimap(config.getStringList(path)),
          ConfDataType::parseSetMultimap);

  static ConfDataType<Protocol> ProtocolEnum =
      new ConfDataType<>(
          "Protocol",
          Protocol.class,
          new EnumGetter<>(Protocol.class),
          (s) -> {
            try {
              return Protocol.valueOf(s);
            } catch (Exception e) {
              String failMsg = String.format("%s is not a valid value for desired key\n", s);
              throw new PlatformServiceException(BAD_REQUEST, failMsg + e.getMessage());
            }
          });

  static ConfDataType<VersionCheckMode> VersionCheckModeEnum =
      new ConfDataType<>(
          "VersionCheckMode",
          VersionCheckMode.class,
          new EnumGetter<>(VersionCheckMode.class),
          (s) -> {
            try {
              return VersionCheckMode.valueOf(s);
            } catch (Exception e) {
              String failMsg = String.format("%s is not a valid value for desired key\n", s);
              throw new PlatformServiceException(BAD_REQUEST, failMsg + e.getMessage());
            }
          });
  static ConfDataType<SkipCertValidationType> SkipCertValdationEnum =
      new ConfDataType<>(
          "SkipCertValidationType",
          SkipCertValidationType.class,
          new EnumGetter<>(SkipCertValidationType.class),
          (s) -> {
            try {
              return SkipCertValidationType.valueOf(s);
            } catch (Exception e) {
              String failMsg = String.format("%s is not a valid value for desired key\n", s);
              throw new PlatformServiceException(BAD_REQUEST, failMsg + e.getMessage());
            }
          });
  static ConfDataType<SearchScope> LdapSearchScopeEnum =
      new ConfDataType<>(
          "LdapSearchScope",
          SearchScope.class,
          new EnumGetter<>(SearchScope.class),
          (s) -> {
            try {
              return SearchScope.valueOf(s);
            } catch (IllegalArgumentException e) {
              String failMsg = String.format("%s is not a valid LDAP Search Scope\n", s);
              throw new PlatformServiceException(BAD_REQUEST, failMsg + e.getMessage());
            }
          });
  static ConfDataType<Role> UserRoleEnum =
      new ConfDataType<>(
          "UserDefaultRole",
          Role.class,
          new EnumGetter<>(Role.class),
          (s) -> {
            try {
              Role defaultRole = Role.valueOf(s);
              if (defaultRole != Role.ConnectOnly && defaultRole != Role.ReadOnly) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format("%s role cannot be set as default user role!", defaultRole));
              }
              return defaultRole;
            } catch (IllegalArgumentException e) {
              String failMsg = String.format("%s is not a valid system role!\n", s);
              throw new PlatformServiceException(BAD_REQUEST, failMsg + e.getMessage());
            }
          });
  static ConfDataType<TlsProtocol> LdapTlsProtocol =
      new ConfDataType<>(
          "LdapTlsProtocol",
          TlsProtocol.class,
          new EnumGetter<>(TlsProtocol.class),
          (s) -> {
            try {
              return TlsProtocol.valueOf(s);
            } catch (IllegalArgumentException e) {
              String failMsg = String.format("%s is not a supported LDAP TLS protocol\n", s);
              throw new PlatformServiceException(BAD_REQUEST, failMsg + e.getMessage());
            }
          });

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

  public static List<ConfKeyTags> parseTagsList(String s) {
    try {
      List<ConfKeyTags> tagList =
          Json.mapper().readValue(s, new TypeReference<List<ConfKeyTags>>() {});
      return tagList;
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Not a valid list of tags."
              + "All possible tags are "
              + "PUBLIC, UIDriven, BETA, INTERNAL, YBM");
    }
  }

  public static SetMultimap<String, String> parseSetMultimap(String s) {
    List<String> tagsValues = parseStringAndApply(s, Config::getStringList);
    if (tagsValues.stream()
        .map(tagAndValue -> tagAndValue.split(":"))
        .anyMatch(
            tagAndValue ->
                tagAndValue.length != 2
                    || StringUtils.isBlank(tagAndValue[0])
                    || StringUtils.isBlank(tagAndValue[1])))
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Not a valid list of user tags and accepted values. "
              + "Accepts a list of enforced_tag:accepted_value pairs.");
    return getSetMultimap(tagsValues);
  }

  private static SetMultimap<String, String> getSetMultimap(List<String> tagValuesList) {
    return tagValuesList.stream()
        .map(s -> s.split(":"))
        .collect(
            ImmutableSetMultimap.toImmutableSetMultimap(
                tagAndValue -> tagAndValue[0].trim(), tagAndValue -> tagAndValue[1].trim()));
  }

  private static <K> K parseStringAndApply(String s, BiFunction<Config, String, K> parser) {
    try {
      Config c = ConfigFactory.parseString("key = " + s);
      return parser.apply(c, "key");
    } catch (Exception e) {
      String failMsg = String.format("%s is not a valid value for desired key\n", s);
      throw new PlatformServiceException(BAD_REQUEST, failMsg + e.getMessage());
    }
  }

  private static String parseString(String s) {
    if (s.isEmpty()) return s;
    return parseStringAndApply(s, Config::getString);
  }
}
