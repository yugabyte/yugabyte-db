// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase.KubernetesPlacement;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.operator.annotations.BlockOperatorResource;
import com.yugabyte.yw.common.operator.annotations.OperatorResourceTypes;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.KubernetesOverridesResponse;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.PlacementInfo;
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
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "KubernetesOverridesController",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class KubernetesOverridesController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesOverridesController.class);

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
    return PlatformResults.withData(validateKubernetesOverrides(taskParams));
  }

  // RFC: In the following method we are not running 'helm template' for every AZ in overrides so if
  // there is bad config in overrides in one of the az configs and if that az is not in placement
  // during the universe creation, we may not report errors but later during edit universe user can
  // add that az into placement which has bad config in overrides, then we might hit the errors edit
  // tasks.

  // Returns errors in overrides and helm template response if it fails.
  private KubernetesOverridesResponse validateKubernetesOverrides(
      UniverseConfigureTaskParams taskParams) {
    Set<String> overrideErrorsSet = new HashSet<>();
    try {
      // Check if read cluster has any overrides specified.
      if (taskParams.getReadOnlyClusters().size() != 0) {
        UserIntent readClusterIntent = taskParams.getReadOnlyClusters().get(0).userIntent;
        if (StringUtils.isNotBlank(readClusterIntent.universeOverrides)
            || readClusterIntent.azOverrides != null && readClusterIntent.azOverrides.size() != 0) {
          overrideErrorsSet.add("Read only cluster is not allowed to have overrides");
        }
      }

      UserIntent userIntent = taskParams.getPrimaryCluster().userIntent;
      Map<String, String> azsOverrides = userIntent.azOverrides;
      if (azsOverrides == null) {
        azsOverrides = new HashMap<>();
      }

      Map<String, Object> universeOverrides = new HashMap<>();
      try {
        universeOverrides = HelmUtils.convertYamlToMap(userIntent.universeOverrides);
      } catch (Exception e) {
        LOG.error("Error in convertYamlToMap: ", e);
        overrideErrorsSet.add(
            "Error: Unable to parse overrides structure, incorrect format specified");
      }
      Set<String> providersAZSet = new HashSet<>();
      Set<String> placementAZSet = new HashSet<>();
      // For every AZ, run helm template with overrides and collect errors.
      for (Cluster cluster : taskParams.clusters) {
        String ybSoftwareVersion = cluster.userIntent.ybSoftwareVersion;
        PlacementInfo pi = cluster.placementInfo;
        Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
        for (Region r : provider.getRegions()) {
          for (AvailabilityZone az : r.getZones()) {
            providersAZSet.add(az.getCode());
          }
        }
        KubernetesPlacement placement =
            new KubernetesPlacement(pi, cluster.clusterType == ClusterType.ASYNC);
        for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {
          UUID azUUID = entry.getKey();
          boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
          String azName = AvailabilityZone.getOrBadRequest(azUUID).getCode();
          String azCode = isMultiAz ? azName : null;
          placementAZSet.add(azName);
          Map<String, String> config = entry.getValue();
          String azOverridesStr =
              azsOverrides.get(PlacementInfoUtil.getAZNameFromUUID(provider, azUUID));
          Map<String, Object> azOverrides = new HashMap<>();
          try {
            azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);
          } catch (Exception e) {
            String errMsg =
                String.format("Error in parsing %s overrides: %s", azName, e.getMessage());
            LOG.error("Error in convertYamlToMap ", e);
            overrideErrorsSet.add(errMsg);
          }

          String namespace =
              KubernetesUtil.getKubernetesNamespace(
                  taskParams.nodePrefix,
                  azCode,
                  config,
                  true, /* newNamingStyle */
                  cluster.clusterType == ClusterType.ASYNC);
          Set<String> partialErrorsSet =
              kubernetesManagerFactory
                  .getManager()
                  .validateOverrides(
                      ybSoftwareVersion, config, namespace, universeOverrides, azOverrides, azName);
          overrideErrorsSet.addAll(partialErrorsSet);
        }
      }

      // Check user provided az which is not in provider(s).
      Set<String> unknownAZs = Sets.difference(azsOverrides.keySet(), providersAZSet);
      if (!unknownAZs.isEmpty()) {
        overrideErrorsSet.add(
            String.format(
                "Provider(s) don't have following AZs: %s. But they are referred in AZ overrides",
                unknownAZs));
      }

      // Check for AZs which are not in placement(s).
      Set<String> extraAZs = Sets.difference(azsOverrides.keySet(), placementAZSet);
      if (!extraAZs.isEmpty()) {
        overrideErrorsSet.add(
            String.format(
                "AZ overrides have following AZs: %s. But no DB pods assigned to these AZs."
                    + "These overrides are not validated.",
                extraAZs));
      }
      return KubernetesOverridesResponse.convertErrorsToKubernetesOverridesResponse(
          overrideErrorsSet);
    } catch (Exception e) {
      LOG.error("Exception in validating kubernetes overrides: ", e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }
}
