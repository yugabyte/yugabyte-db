// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.logging.LogUtil;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.forms.PlatformLoggingConfig;
import com.yugabyte.yw.forms.PlatformResults;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.text.SimpleDateFormat;
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
      nickname = "setLoggingSettings")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Logging Config",
        value = "Logging config to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.PlatformLoggingConfig",
        paramType = "body")
  })
  public Result setLoggingSettings() throws JoranException {
    PlatformLoggingConfig data =
        formFactory.getFormDataOrBadRequest(PlatformLoggingConfig.class).get();
    String newLevel = data.getLevel().toString();
    String newRolloverPattern = data.getRolloverPattern();
    if (newRolloverPattern != null) {
      try {
        new SimpleDateFormat(newRolloverPattern);
      } catch (Exception e) {
        throw new PlatformServiceException(BAD_REQUEST, "Incorrect pattern " + newRolloverPattern);
      }
    }
    Integer newMaxHistory = data.getMaxHistory();
    LogUtil.updateLoggingContext(newLevel, newRolloverPattern, newMaxHistory);
    LogUtil.updateLoggingConfig(sConfigFactory, newLevel, newRolloverPattern, newMaxHistory);
    return PlatformResults.withData(data);
  }
}
