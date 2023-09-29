// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Inject;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.logging.LogUtil;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.AuditLoggingConfig;
import com.yugabyte.yw.forms.PlatformLoggingConfig;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.text.SimpleDateFormat;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "LoggingConfig",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class LoggingConfigController extends AuthenticatedController {

  public static final org.slf4j.Logger LOG = LoggerFactory.getLogger(LoggingConfigController.class);

  @Inject ValidatingFormFactory formFactory;

  @Inject BeanValidator validator;

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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result setLoggingSettings(Http.Request request) throws JoranException {
    PlatformLoggingConfig data =
        formFactory.getFormDataOrBadRequest(request, PlatformLoggingConfig.class).get();
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
    String applicationLogFileNamePrefix = data.getFileNamePrefix();
    LogUtil.updateApplicationLoggingContext(
        newLevel, newRolloverPattern, newMaxHistory, applicationLogFileNamePrefix);
    LogUtil.updateApplicationLoggingConfig(
        sConfigFactory, newLevel, newRolloverPattern, newMaxHistory, applicationLogFileNamePrefix);
    return PlatformResults.withData(data);
  }

  @ApiOperation(
      value = "Set Audit Logging Level",
      response = AuditLoggingConfig.class,
      nickname = "setAuditLoggingSettings")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Audit Logging Config",
        value = "Audit Logging config to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.AuditLoggingConfig",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result setAuditLoggingSettings(Http.Request request) throws JoranException {
    AuditLoggingConfig data =
        formFactory.getFormDataOrBadRequest(request, AuditLoggingConfig.class).get();
    data.validate(validator);
    LogUtil.updateAuditLoggingContext(data);
    LogUtil.updateAuditLoggingConfig(sConfigFactory, data);
    return PlatformResults.withData(data);
  }
}
