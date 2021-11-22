// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Inject;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.logging.LogUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.forms.PlatformLoggingConfig;
import com.yugabyte.yw.forms.PlatformResults;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.lang.System;
import org.slf4j.LoggerFactory;
import play.mvc.Controller;
import play.mvc.Result;

@Api(
    value = "LoggingConfig",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class LoggingConfigController extends Controller {

  public static final org.slf4j.Logger LOG = LoggerFactory.getLogger(LoggingConfigController.class);

  @Inject ValidatingFormFactory formFactory;

  @Inject SettableRuntimeConfigFactory sConfigFactory;

  @ApiOperation(
      value = "Set Logging Level",
      response = PlatformLoggingConfig.class,
      nickname = "setLoggingLevel")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Logging Config",
        value = "Logging config to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.PlatformLoggingConfig",
        paramType = "body")
  })
  public Result setLoggingLevel() throws JoranException {
    PlatformLoggingConfig data =
        formFactory.getFormDataOrBadRequest(PlatformLoggingConfig.class).get();
    String newLevel = data.getLevel().toString();
    LogUtil.setLoggingLevel(newLevel);
    LogUtil.setLoggingConfig(sConfigFactory, newLevel);
    return PlatformResults.withData(data);
  }
}
