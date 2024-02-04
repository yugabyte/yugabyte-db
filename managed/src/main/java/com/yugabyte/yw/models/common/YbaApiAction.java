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
import com.google.re2j.Matcher;
import com.google.re2j.Pattern;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigException;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiParam;
import java.lang.reflect.AnnotatedElement;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Parameter;
import java.util.UUID;
import java.util.concurrent.CompletionStage;
import play.mvc.Action;
import play.mvc.Http.Request;
import play.mvc.Http.Status;
import play.mvc.Result;

public class YbaApiAction extends Action<YbaApi> {
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private RuntimeConfigCache cache;

  private static final String UUID_PATTERN =
      "([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})";

  @Override
  public CompletionStage<Result> call(Request req) {
    processStrictMode();
    processSafeMode();
    processFeatureRuntimeConfig(req);
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
  private void processFeatureRuntimeConfig(Request request) {
    String featureRuntimeConfig = configuration.runtimeConfig();
    if (Strings.isNullOrEmpty(featureRuntimeConfig)) {
      return;
    }
    switch (configuration.runtimeConfigScope()) {
      case GLOBAL:
        validateGlobalRuntimeConfig(request.path(), featureRuntimeConfig);
        break;
      case CUSTOMER:
        validateCustomerRuntimeConfig(request.path(), featureRuntimeConfig);
        break;
      case UNIVERSE:
        validateUniverseRuntimeConfig(request.path(), featureRuntimeConfig);
        break;
      case PROVIDER:
        validateProviderRuntimeConfig(request.path(), featureRuntimeConfig);
        break;
      default:
        throw new PlatformServiceException(
            Status.INTERNAL_SERVER_ERROR, "Incorrect scope for runtime config");
    }
  }

  private void validateGlobalRuntimeConfig(String requestPath, String featureRuntimeConfig) {
    validateRuntimeConfig(runtimeConfigFactory.globalRuntimeConf(), featureRuntimeConfig);
  }

  private void validateCustomerRuntimeConfig(String requestPath, String featureRuntimeConfig) {
    UUID customerUUID = null;
    Pattern customerRegex = Pattern.compile(String.format(".*/customers/%s/?.*", UUID_PATTERN));
    Matcher customerMatcher = customerRegex.matcher(requestPath);
    if (customerMatcher.find()) {
      customerUUID = UUID.fromString(customerMatcher.group(1));
    }
    if (customerUUID == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Could not find customer uuid in request path");
    }
    Customer customer = Customer.getOrBadRequest(customerUUID);
    validateRuntimeConfig(runtimeConfigFactory.forCustomer(customer), featureRuntimeConfig);
  }

  private void validateUniverseRuntimeConfig(String requestPath, String featureRuntimeConfig) {
    UUID universeUUID = null;
    Pattern universeRegex = Pattern.compile(String.format(".*/universes/%s/?.*", UUID_PATTERN));
    Matcher universeMatcher = universeRegex.matcher(requestPath);
    if (universeMatcher.find()) {
      universeUUID = UUID.fromString(universeMatcher.group(1));
    }
    if (universeUUID == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Could not find universe uuid in request path");
    }
    Universe universe = Universe.getOrBadRequest(universeUUID);
    validateRuntimeConfig(runtimeConfigFactory.forUniverse(universe), featureRuntimeConfig);
  }

  private void validateProviderRuntimeConfig(String requestPath, String featureRuntimeConfig) {
    UUID providerUUID = null;
    Pattern providerRegex = Pattern.compile(String.format(".*/providers/%s/?.*", UUID_PATTERN));
    Matcher providerMatcher = providerRegex.matcher(requestPath);
    if (providerMatcher.find()) {
      providerUUID = UUID.fromString(providerMatcher.group(1));
    }
    if (providerUUID == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Could not find provider uuid in request path");
    }
    Provider provider = Provider.getOrBadRequest(providerUUID);
    validateRuntimeConfig(runtimeConfigFactory.forProvider(provider), featureRuntimeConfig);
  }

  private void validateRuntimeConfig(Config config, String featureRuntimeConfig) {
    if (!config.hasPath(featureRuntimeConfig)) {
      String err =
          String.format(
              "%s is not annotated with a valid feature flag: %s",
              delegate.annotatedElement, featureRuntimeConfig);
      throw new PlatformServiceException(Status.BAD_REQUEST, err);
    }
    try {
      if (!config.getBoolean(featureRuntimeConfig)) {
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
