// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.operator.annotations.BlockOperatorResource;
import com.yugabyte.yw.common.operator.annotations.OperatorResourceTypes;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.KubernetesOverridesHandler;
import com.yugabyte.yw.forms.KubernetesOverridesResponse;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.models.Customer;
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
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "KubernetesOverridesController",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class KubernetesOverridesController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesOverridesController.class);

  @Inject private KubernetesOverridesHandler kubernetesOverridesHandler;

  @Inject private KubernetesManagerFactory kubernetesManagerFactory;

  @ApiOperation(
      value = "Validate kubernetes overrides.",
      notes = "Returns possible errors.",
      nickname = "validateKubernetesOverrides",
      response = KubernetesOverridesResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UniverseConfigureTaskParams",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.UniverseConfigureTaskParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation =
            @Resource(path = Util.UNIVERSE_UUID, sourceType = SourceType.REQUEST_BODY))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result validateKubernetesOverrides(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    UniverseConfigureTaskParams taskParams =
        parseJsonAndValidate(request, UniverseConfigureTaskParams.class);
    return PlatformResults.withData(
        kubernetesOverridesHandler.validateKubernetesOverrides(taskParams));
  }
}
