// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.utils.NaturalOrderComparator;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.PredicateParam;
import java.util.Iterator;
import java.util.function.Function;
import java.util.function.Predicate;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Builder;
import lombok.extern.slf4j.Slf4j;
import play.api.Play;
import play.libs.Json;

@Singleton
@Slf4j
public class NodeConfigValidator {
  public static final String CONFIG_KEY_FORMAT = "yb.node_agent.preflight_checks.%s";

  private RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public NodeConfigValidator(RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  /* Placeholder key for retrieving a config value. */
  @Builder
  static class ConfigKey {
    private String path;
    private Provider provider;

    @Override
    public String toString() {
      return String.format(
          "%s(path=%s, provider=%s)", getClass().getSimpleName(), path, provider.uuid);
    }
  }

  private final Function<Provider, Config> PROVIDER_CONFIG =
      provider -> runtimeConfigFactory.forProvider(provider);

  /** Supplier for an Integer value from application config file. */
  public final Function<ConfigKey, Integer> CONFIG_INT_SUPPLIER =
      key -> PROVIDER_CONFIG.apply(key.provider).getInt(key.path);

  /** Supplier for a String value from application config file. */
  public final Function<ConfigKey, String> CONFIG_STRING_SUPPLIER =
      key -> PROVIDER_CONFIG.apply(key.provider).getString(key.path);

  /** Supplier for a boolean value from application config file. */
  public final Function<ConfigKey, Boolean> CONFIG_BOOL_SUPPLIER =
      key -> PROVIDER_CONFIG.apply(key.provider).getBoolean(key.path);

  /** Predicates to validate different data types. */
  public enum PredicateType {
    STRING_EQUALS {
      @Override
      public Predicate<PredicateParam> withPathSuffix(String suffix) {
        return p ->
            p.getValue()
                .equalsIgnoreCase(getFromConfig(instance().CONFIG_STRING_SUPPLIER, p, suffix));
      }
    },
    GREATER_EQUAL {
      @Override
      public Predicate<PredicateParam> withPathSuffix(String suffix) {
        return p ->
            Integer.parseInt(p.getValue())
                >= getFromConfig(instance().CONFIG_INT_SUPPLIER, p, suffix);
      }
    },
    MIN_VERSION {
      @Override
      public Predicate<PredicateParam> withPathSuffix(String suffix) {
        return p ->
            new NaturalOrderComparator()
                    .compare(
                        p.getValue(), getFromConfig(instance().CONFIG_STRING_SUPPLIER, p, suffix))
                >= 0;
      }
    },
    JSON_STRINGS_EQUAL {
      @Override
      public Predicate<PredicateParam> withPathSuffix(String suffix) {
        return p -> {
          try {
            String val = getFromConfig(instance().CONFIG_STRING_SUPPLIER, p, suffix);
            JsonNode node = Json.parse(p.getValue());
            if (!node.isObject()) {
              return false;
            }
            ObjectNode object = (ObjectNode) node;
            Iterator<String> iter = object.fieldNames();
            while (iter.hasNext()) {
              String value = iter.next();
              if (!val.equalsIgnoreCase(object.get(value).asText())) {
                return false;
              }
            }
          } catch (Exception e) {
            return false;
          }
          return true;
        };
      }
    };

    protected abstract Predicate<PredicateParam> withPathSuffix(String suffix);
  }

  private static NodeConfigValidator instance() {
    return Play.current().injector().instanceOf(NodeConfigValidator.class);
  }

  private static <T extends Comparable<T>> T getFromConfig(
      Function<ConfigKey, T> function, PredicateParam param, String key) {
    ConfigKey configKey =
        ConfigKey.builder()
            .provider(param.getProvider())
            .path(String.format(CONFIG_KEY_FORMAT, key))
            .build();
    T value = function.apply(configKey);
    log.debug("Value for {}: {}", configKey, value);
    return value;
  }
}
