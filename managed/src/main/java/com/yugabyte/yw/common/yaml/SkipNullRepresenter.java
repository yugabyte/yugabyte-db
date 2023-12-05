/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.yaml;

import org.yaml.snakeyaml.DumperOptions;
import org.yaml.snakeyaml.introspector.Property;
import org.yaml.snakeyaml.nodes.NodeTuple;
import org.yaml.snakeyaml.nodes.Tag;
import org.yaml.snakeyaml.representer.Representer;

public class SkipNullRepresenter extends Representer {

  public SkipNullRepresenter() {
    this(new DumperOptions());
  }

  public SkipNullRepresenter(DumperOptions options) {
    super(options);
  }

  protected NodeTuple representJavaBeanProperty(
      Object javaBean, Property property, Object propertyValue, Tag customTag) {
    // if value of property is null, ignore it.
    if (propertyValue == null) {
      return null;
    } else {
      return super.representJavaBeanProperty(javaBean, property, propertyValue, customTag);
    }
  }
}
