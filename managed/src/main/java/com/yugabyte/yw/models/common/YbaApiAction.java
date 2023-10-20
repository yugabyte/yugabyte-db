/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.common;

import com.google.common.base.Strings;
import com.google.inject.Inject;
import com.typesafe.config.ConfigException;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiParam;
import java.lang.reflect.AnnotatedElement;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Parameter;
import java.util.concurrent.CompletionStage;
import play.mvc.Action;
import play.mvc.Http.Request;
import play.mvc.Http.Status;
import play.mvc.Result;

public class YbaApiAction extends Action<YbaApi> {
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private RuntimeConfigCache cache;

  @Override
  public CompletionStage<Result> call(Request req) {
    processStrictMode();
    processSafeMode();
    processFeatureRuntimeConfig();
    return delegate.call(req);
  }

  private void processStrictMode() {
    if (configuration.visibility() == YbaApi.YbaApiVisibility.DEPRECATED) {
      String strictModeKey = GlobalConfKeys.ybaApiStrictMode.getKey();
      boolean strictMode = cache.getBoolean(strictModeKey);
      if (strictMode) {
        String err =
            String.format("Not allowed in strict mode. %s", getDescription(annotatedElement));
        throw new PlatformServiceException(Status.BAD_REQUEST, err);
      }
    }
  }

  private void processSafeMode() {
    if (configuration.visibility() == YbaApi.YbaApiVisibility.PREVIEW) {
      String safeModeKey = GlobalConfKeys.ybaApiSafeMode.getKey();
      boolean safeMode = cache.getBoolean(safeModeKey);
      if (safeMode) {
        String err =
            String.format("Not allowed in safe mode. %s", getDescription(annotatedElement));
        throw new PlatformServiceException(Status.BAD_REQUEST, err);
      }
    }
  }

  private String getDescription(AnnotatedElement annotatedElement) {
    if (annotatedElement.getClass().equals(Method.class)) {
      ApiOperation op = annotatedElement.getAnnotation(ApiOperation.class);
      if (op != null) {
        return op.value();
      }
    } else if (annotatedElement.getClass().equals(Parameter.class)) {
      ApiParam p = annotatedElement.getAnnotation(ApiParam.class);
      if (p != null) {
        return p.value();
      }
    } else if (annotatedElement.getClass().equals(Field.class)) {
      ApiModelProperty p = annotatedElement.getAnnotation(ApiModelProperty.class);
      if (p != null) {
        return p.value();
      }
    }
    return "";
  }

  // process the feature flag runtimeConfig for the YbaApi annotation
  private void processFeatureRuntimeConfig() {
    String featureRuntimeConfig = configuration.runtimeConfig();
    if (Strings.isNullOrEmpty(featureRuntimeConfig)) {
      return;
    }
    if (!runtimeConfigFactory.globalRuntimeConf().hasPath(featureRuntimeConfig)) {
      String err =
          String.format(
              "%s is not annotated with a valid feature flag: %s",
              delegate.annotatedElement, featureRuntimeConfig);
      throw new PlatformServiceException(Status.BAD_REQUEST, err);
    }
    try {
      if (!runtimeConfigFactory.globalRuntimeConf().getBoolean(featureRuntimeConfig)) {
        String err = String.format("%s feature flag is not enabled", featureRuntimeConfig);
        throw new PlatformServiceException(Status.BAD_REQUEST, err);
      }
    } catch (ConfigException.WrongType | ConfigException.Missing e) {
      String err =
          String.format("%s feature flag is not set to a boolean value", featureRuntimeConfig);
      throw new PlatformServiceException(Status.BAD_REQUEST, err);
    }
  }
}
