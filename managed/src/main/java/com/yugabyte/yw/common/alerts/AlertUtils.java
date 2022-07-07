// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class AlertUtils {
  public static final Logger LOG = LoggerFactory.getLogger(AlertUtils.class);

  public static String getJsonTypeName(AlertChannelParams params) {
    Class<?> clz = params.getClass();
    JsonTypeName an = clz.getDeclaredAnnotation(JsonTypeName.class);
    return an.value();
  }
}
