// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.util.ContextInitializer;
import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.LoggingConfigFormData;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.lang.System;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.mvc.Controller;
import play.mvc.Result;
import play.mvc.Results;

@Api(
    value = "LoggingConfig",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class LoggingConfigController extends Controller {

  public static final org.slf4j.Logger LOG = LoggerFactory.getLogger(LoggingConfigController.class);

  @Inject ValidatingFormFactory formFactory;

  @ApiOperation(value = "Set Logging Level", nickname = "setLoggingLevel")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Logging Config",
        value = "Logging config to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.LoggingConfigFormData",
        paramType = "body")
  })
  public Result setLoggingLevel() {
    LoggingConfigFormData data =
        formFactory.getFormDataOrBadRequest(LoggingConfigFormData.class).get();
    String newLevel = data.getLevel();
    Set<String> levels = new HashSet<>(Arrays.asList("TRACE", "DEBUG", "INFO", "WARN", "ERROR"));
    if (!levels.contains(newLevel)) {
      throw new PlatformServiceException(BAD_REQUEST, "Level must be one of " + levels.toString());
    }

    if (!newLevel.equals(System.getProperty("APPLICATION_LOG_LEVEL"))) {
      System.setProperty("APPLICATION_LOG_LEVEL", newLevel);
      LoggerContext loggerContext = (LoggerContext) LoggerFactory.getILoggerFactory();
      loggerContext.reset();
      ContextInitializer ci = new ContextInitializer(loggerContext);
      try {
        ci.autoConfig();
      } catch (JoranException ex) {
        LOG.error("Could not run autoconfig", ex);
        throw new PlatformServiceException(BAD_REQUEST, ex.getMessage());
      }
    }
    return Results.status(OK, String.format("Successfully set logging level to %s", newLevel));
  }
}
