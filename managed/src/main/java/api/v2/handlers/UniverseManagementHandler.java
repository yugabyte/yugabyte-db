// Copyright (c) Yugabyte, Inc.

package api.v2.handlers;

import api.v2.mappers.ClusterMapper;
import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.mappers.UniverseRespMapper;
import api.v2.models.ClusterAddSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.ClusterSpec.ClusterTypeEnum;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseDeleteSpec;
import api.v2.models.UniverseEditSpec;
import api.v2.models.UniverseSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler.OpType;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Request;

@Slf4j
public class UniverseManagementHandler extends ApiControllerUtils {
  @Inject private RuntimeConfGetter confGetter;

  @Inject private UniverseCRUDHandler universeCRUDHandler;
  @Inject private Commissioner commissioner;

  public api.v2.models.Universe getUniverse(UUID cUUID, UUID uniUUID)
      throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    // get v1 Universe
    com.yugabyte.yw.forms.UniverseResp v1Response =
        com.yugabyte.yw.forms.UniverseResp.create(universe, null, confGetter);
    log.info("Getting Universe with UUID: {}", uniUUID);
    // map to v2 Universe
    api.v2.models.Universe v2Response = UniverseRespMapper.INSTANCE.toV2Universe(v1Response);
    log.trace("Got Universe {}", prettyPrint(v2Response));
    return v2Response;
  }

  public YBATask createUniverse(Request request, UUID cUUID, UniverseCreateSpec universeSpec) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    log.info("Create Universe with v2 spec: {}", prettyPrint(universeSpec));
    // map universeSpec to v1 universe details
    UniverseDefinitionTaskParams v1DefnParams =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV1UniverseDefinitionTaskParamsFromCreateSpec(
            universeSpec);
    UniverseConfigureTaskParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toUniverseConfigureTaskParams(
            v1DefnParams, request);
    log.debug("Create Universe translated to v1 spec: {}", prettyPrint(v1Params));
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
    return new YBATask().resourceUuid(universeResp.universeUUID).taskUuid(universeResp.taskUUID);
  }

  private UniverseEditSpec inheritFromPrimaryBeforeMapping(
      UniverseEditSpec universeEditSpec, ClusterSpec primaryCluster) {
    if (primaryCluster == null) {
      return universeEditSpec;
    }

    List<ClusterEditSpec> clusters = new ArrayList<>();
    for (ClusterEditSpec cluster : universeEditSpec.getClusters()) {
      if (cluster.getUuid().equals(primaryCluster.getUuid())) {
        clusters.add(cluster);
        continue;
      }
      ClusterEditSpec merged = new ClusterEditSpec();
      ClusterMapper.INSTANCE.deepCopyClusterEditSpecWithoutPlacementSpec(primaryCluster, merged);
      ClusterMapper.INSTANCE.deepCopyClusterEditSpec(cluster, merged);
      clusters.add(merged);
    }
    universeEditSpec.clusters(clusters);
    return universeEditSpec;
  }

  public YBATask editUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseEditSpec universeEditSpec) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe dbUniverse = Universe.getOrBadRequest(uniUUID);
    log.info("Edit Universe with v2 spec: {}", prettyPrint(universeEditSpec));
    // inherit RR cluster properties from primary cluster in given edit spec
    UniverseSpec v2Universe =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV2UniverseSpec(
            dbUniverse.getUniverseDetails());
    ClusterSpec primaryV2Cluster =
        v2Universe.getClusters().stream()
            .filter(c -> c.getClusterType().equals(ClusterTypeEnum.PRIMARY))
            .findAny()
            .orElse(null);
    universeEditSpec = inheritFromPrimaryBeforeMapping(universeEditSpec, primaryV2Cluster);
    boolean isRREdited =
        universeEditSpec.getClusters().stream()
            .filter(c -> !c.getUuid().equals(primaryV2Cluster.getUuid()))
            .findAny()
            .isPresent();
    // map universeEditSpec to v1 universe details
    UniverseDefinitionTaskParams v1DefnParams =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV1UniverseDefinitionTaskParamsFromEditSpec(
            universeEditSpec, dbUniverse.getUniverseDetails());
    UniverseConfigureTaskParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toUniverseConfigureTaskParams(
            v1DefnParams, request);
    log.debug("Edit Universe translated to v1 spec: {}", prettyPrint(v1Params));

    // edit universe with v1 spec
    v1Params.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.EDIT;
    v1Params.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, v1Params);
    // Handle ASYNC cluster edit
    if (isRREdited) {
      v1Params.currentClusterType = ClusterType.ASYNC;
      universeCRUDHandler.configure(customer, v1Params);
    }
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
    return new YBATask().resourceUuid(uniUUID).taskUuid(taskUUID);
  }

  public YBATask addCluster(
      Request request, UUID cUUID, UUID uniUUID, ClusterAddSpec clusterAddSpec) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe dbUniverse = Universe.getOrBadRequest(uniUUID);
    log.info("Add cluster to Universe with v2 spec: {}", prettyPrint(clusterAddSpec));

    UniverseConfigureTaskParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toUniverseConfigureTaskParams(
            dbUniverse.getUniverseDetails(), request);
    v1Params.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    v1Params.currentClusterType = ClusterType.ASYNC;
    // to construct the new v1 cluster, start with a copy of primary cluster
    Cluster primaryCluster = dbUniverse.getUniverseDetails().getPrimaryCluster();
    Cluster newReadReplica = new Cluster(ClusterType.ASYNC, primaryCluster.userIntent);
    // overwrite the copy of primary cluster with user provided spec for read replica
    newReadReplica.setUuid(UUID.randomUUID());
    newReadReplica = ClusterMapper.INSTANCE.overwriteClusterAddSpec(clusterAddSpec, newReadReplica);
    // prepare the v1Params with only the read replica cluster in the payload
    v1Params.clusters.clear();
    v1Params.clusters.add(newReadReplica);
    universeCRUDHandler.configure(customer, v1Params);
    // start the add cluster task
    UUID taskUUID = universeCRUDHandler.createCluster(customer, dbUniverse, v1Params);
    return new YBATask().resourceUuid(newReadReplica.uuid).taskUuid(taskUUID);
  }

  public YBATask deleteReadReplicaCluster(
      UUID cUUID, UUID uniUUID, UUID clsUUID, Boolean forceDelete) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    UUID taskUUID = universeCRUDHandler.clusterDelete(customer, universe, clsUUID, forceDelete);
    return new YBATask().resourceUuid(uniUUID).taskUuid(taskUUID);
  }

  public YBATask deleteUniverse(UUID cUUID, UUID uniUUID, UniverseDeleteSpec universeDeleteSpec)
      throws JsonProcessingException {
    boolean isForceDelete =
        universeDeleteSpec != null ? universeDeleteSpec.getIsForceDelete() : false;
    boolean isDeleteBackups =
        universeDeleteSpec != null ? universeDeleteSpec.getIsDeleteBackups() : false;
    boolean isDeleteAssociatedCerts =
        universeDeleteSpec != null ? universeDeleteSpec.getIsDeleteAssociatedCerts() : false;
    log.info("Starting v2 delete universe with UUID: {}", uniUUID);
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    UUID taskUuid =
        universeCRUDHandler.destroy(
            customer, universe, isForceDelete, isDeleteBackups, isDeleteAssociatedCerts);
    // construct a v2 Task to return from here
    YBATask ybaTask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());

    log.info("Started delete universe task {}", prettyPrint(ybaTask));
    return ybaTask;
  }
}
