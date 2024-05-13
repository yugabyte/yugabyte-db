// Copyright (c) Yugabyte, Inc.
package api.v2.handlers;

import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.mappers.UniverseRespMapper;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseEditSpec;
import api.v2.models.UniverseResp;
import api.v2.models.YBPTask;
import api.v2.utils.ApiControllerUtils;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler.OpType;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class UniverseManagementHandler extends ApiControllerUtils {
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private UniverseCRUDHandler universeCRUDHandler;
  @Inject private Commissioner commissioner;

  public UniverseResp getUniverse(UUID cUUID, UUID uniUUID) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    // get v1 Universe
    com.yugabyte.yw.forms.UniverseResp v1Response =
        com.yugabyte.yw.forms.UniverseResp.create(
            universe, null, runtimeConfigFactory.globalRuntimeConf());
    // map to v2 Universe
    UniverseResp v2Response = UniverseRespMapper.INSTANCE.toV2UniverseResp(v1Response);
    return v2Response;
  }

  public YBPTask createUniverse(UUID cUUID, UniverseCreateSpec universeSpec) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    log.info(
        "Create Universe with v2 spec: {}",
        Json.prettyPrint(CommonUtils.maskConfig((ObjectNode) Json.toJson(universeSpec))));
    // map universeSpec to v1 universe details
    UniverseDefinitionTaskParams v1DefnParams =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV1UniverseDefinitionTaskParamsFromCreateSpec(
            universeSpec);
    UniverseConfigureTaskParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toUniverseConfigureTaskParams(v1DefnParams);
    log.debug(
        "Create Universe translated to v1 spec: {}",
        Json.prettyPrint(CommonUtils.maskConfig((ObjectNode) Json.toJson(v1Params))));
    // create universe with v1 spec
    v1Params.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    v1Params.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, v1Params);

    if (v1Params.clusters.stream().anyMatch(cluster -> cluster.clusterType == ClusterType.ASYNC)) {
      v1Params.currentClusterType = ClusterType.ASYNC;
      universeCRUDHandler.configure(customer, v1Params);
    }
    com.yugabyte.yw.forms.UniverseResp universeResp =
        universeCRUDHandler.createUniverse(customer, v1Params);
    return new YBPTask().resourceUuid(universeResp.universeUUID).taskUuid(universeResp.taskUUID);
  }

  public YBPTask editUniverse(UUID cUUID, UUID uniUUID, UniverseEditSpec universeEditSpec) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe dbUniverse = Universe.getOrBadRequest(uniUUID);
    log.info(
        "Edit Universe with v2 spec: {}",
        Json.prettyPrint(CommonUtils.maskConfig((ObjectNode) Json.toJson(universeEditSpec))));
    // map universeEditSpec to v1 universe details
    UniverseDefinitionTaskParams v1DefnParams =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV1UniverseDefinitionTaskParamsFromEditSpec(
            universeEditSpec, dbUniverse.getUniverseDetails());
    UniverseConfigureTaskParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toUniverseConfigureTaskParams(v1DefnParams);
    // v1Params.setUniverseUUID(uniUUID);
    log.debug(
        "Edit Universe translated to v1 spec: {}",
        Json.prettyPrint(CommonUtils.maskConfig((ObjectNode) Json.toJson(v1Params))));
    // edit universe with v1 spec
    v1Params.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.EDIT;
    // TODO: Handle ASYNC cluster edit
    v1Params.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, v1Params);
    universeCRUDHandler.checkGeoPartitioningParameters(customer, v1Params, OpType.UPDATE);

    Cluster primaryCluster = v1Params.getPrimaryCluster();
    for (Cluster readOnlyCluster : dbUniverse.getUniverseDetails().getReadOnlyClusters()) {
      UniverseCRUDHandler.validateConsistency(primaryCluster, readOnlyCluster);
    }

    TaskType taskType = TaskType.EditUniverse;
    if (primaryCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      taskType = TaskType.EditKubernetesUniverse;
      universeCRUDHandler.notHelm2LegacyOrBadRequest(dbUniverse);
      universeCRUDHandler.checkHelmChartExists(primaryCluster.userIntent.ybSoftwareVersion);
    } else {
      universeCRUDHandler.mergeNodeExporterInfo(dbUniverse, v1Params);
    }
    for (Cluster cluster : v1Params.clusters) {
      PlacementInfoUtil.updatePlacementInfo(
          v1Params.getNodesInCluster(cluster.uuid), cluster.placementInfo);
    }
    v1Params.rootCA = universeCRUDHandler.checkValidRootCA(dbUniverse.getUniverseDetails().rootCA);
    UUID taskUUID = commissioner.submit(taskType, v1Params);
    log.info(
        "Submitted {} for {} : {}, task uuid = {}.",
        taskType,
        uniUUID,
        dbUniverse.getName(),
        taskUUID);
    return new YBPTask().resourceUuid(uniUUID).taskUuid(taskUUID);
  }
}
