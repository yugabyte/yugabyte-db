package com.yugabyte.yw.common; // Copyright (c) YugaByte, Inc.

import com.google.inject.Inject;
import com.google.inject.Singleton;
import java.io.InputStream;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.CustomClassLoaderConstructor;
import org.yaml.snakeyaml.LoaderOptions;

@Singleton
public class YamlWrapper {

  private final play.Environment environment;

  @Inject
  public YamlWrapper(play.Environment environment) {
    this.environment = environment;
  }

  /** Load a com.yugabyte.yw.common.Yaml file from the classpath. */
  public <T> T load(String resourceName) {

    return load(environment.resourceAsStream(resourceName), environment.classLoader());
  }

  /**
   * Load the specified InputStream as com.yugabyte.yw.common.Yaml.
   *
   * @param classloader The classloader to use to instantiate Java objects.
   */
  public <T> T load(InputStream is, ClassLoader classloader) {
    LoaderOptions loaderOptions = new LoaderOptions();
    loaderOptions.setTagInspector(globalTagAllowed -> true);
    Yaml yaml = new Yaml(new CustomClassLoaderConstructor(classloader, loaderOptions));
    return yaml.load(is);
  }
}
