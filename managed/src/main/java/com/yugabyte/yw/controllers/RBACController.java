// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.rbac.PermissionInfo;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.PermissionUtil;
import com.yugabyte.yw.forms.PlatformResults;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiParam;
import io.swagger.annotations.Authorization;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.EnumUtils;
import play.mvc.Result;

@Api(
    value = "RBAC management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class RBACController extends AuthenticatedController {

  private final PermissionUtil permissionUtil;

  @Inject
  public RBACController(PermissionUtil permissionUtil) {
    this.permissionUtil = permissionUtil;
  }

  @ApiOperation(
      value = "List all the permissions available",
      nickname = "listPermissions",
      response = PermissionInfo.class,
      responseContainer = "List")
  public Result listPermissions(
      UUID customerUUID,
      @ApiParam(value = "Optional resource type to filter permission list") String resourceType) {
    List<PermissionInfo> permissionInfoList = Collections.emptyList();
    if (EnumUtils.isValidEnum(ResourceType.class, resourceType)) {
      permissionInfoList = permissionUtil.getAllPermissionInfo(ResourceType.valueOf(resourceType));
    } else {
      permissionInfoList = permissionUtil.getAllPermissionInfo();
    }
    return PlatformResults.withData(permissionInfoList);
  }
}
