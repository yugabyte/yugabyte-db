package com.yugabyte.troubleshoot.ts.models;

import java.time.Duration;
import java.util.Map;
import org.springframework.core.env.Environment;

public class RuntimeConfig {

  private final Environment environment;
  private final Map<String, String> customValues;

  public RuntimeConfig(Environment environment, Map<String, String> customValues) {
    this.environment = environment;
    this.customValues = customValues;
  }

  public String getString(RuntimeConfigKey key) {
    if (customValues.containsKey(key.getPath())) {
      return customValues.get(key.getPath());
    }
    return environment.getProperty(key.getPath());
  }

  public int getInt(RuntimeConfigKey key) {
    return Integer.parseInt(getString(key));
  }

  public long getLong(RuntimeConfigKey key) {
    return Long.parseLong(getString(key));
  }

  public double getDouble(RuntimeConfigKey key) {
    return Double.parseDouble(getString(key));
  }

  public Duration getDuration(RuntimeConfigKey key) {
    return Duration.parse(getString(key));
  }
}
