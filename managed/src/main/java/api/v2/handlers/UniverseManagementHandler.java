// Copyright (c) Yugabyte, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.METHOD_NOT_ALLOWED;

import api.v2.mappers.ClusterMapper;
import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.mappers.UniverseRespMapper;
import api.v2.models.AttachUniverseSpec;
import api.v2.models.ClusterAddSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.ClusterSpec.ClusterTypeEnum;
import api.v2.models.DetachUniverseSpec;
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
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler.OpType;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AttachDetachSpec;
import com.yugabyte.yw.models.AttachDetachSpec.PlatformPaths;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.annotation.Transactional;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Request;

@Slf4j
public class UniverseManagementHandler extends ApiControllerUtils {
  @Inject private RuntimeConfGetter confGetter;
  @Inject private ReleaseManager releaseManager;
  @Inject private SwamperHelper swamperHelper;
  @Inject private UniverseCRUDHandler universeCRUDHandler;
  @Inject private Commissioner commissioner;

  private static final String RELEASES_PATH = "yb.releases.path";
  private static final String YBC_RELEASE_PATH = "ybc.docker.release";
  private static final String YBC_RELEASES_PATH = "ybc.releases.path";

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
    if (log.isTraceEnabled()) {
      log.trace("Got Universe {}", prettyPrint(v2Response));
    }
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

  public InputStream detachUniverse(
      Request request, UUID customerUUID, UUID universeUUID, DetachUniverseSpec detachUniverseSpec)
      throws IOException {
    checkAttachDetachEnabled();
    log.debug(
        "Attach Detach Universe spec will include releases: {}",
        !detachUniverseSpec.getSkipReleases());
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));

    List<InstanceType> instanceTypes =
        InstanceType.findByProvider(
            provider,
            confGetter,
            confGetter.getConfForScope(provider, ProviderConfKeys.allowUnsupportedInstances));

    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(universe.getUniverseUUID());
    if (!xClusterConfigs.isEmpty()) {
      throw new PlatformServiceException(
          METHOD_NOT_ALLOWED,
          "Detach universe currently does not support universes with xcluster replication set up.");
    }

    // Validate that universe is in a healthy state, not currently updating, or paused.
    if (universe.getUniverseDetails().updateInProgress
        || !universe.getUniverseDetails().updateSucceeded
        || universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Detach universe is not allowed if universe is currently updating, unhealthy, "
                  + "or in paused state. UpdateInProgress = %b, UpdateSucceeded = %b, "
                  + "UniversePaused = %b",
              universe.getUniverseDetails().updateInProgress,
              universe.getUniverseDetails().updateSucceeded,
              universe.getUniverseDetails().universePaused));
    }

    // Lock Universe to prevent updates from happening.
    universe = Util.lockUniverse(universe);
    AttachDetachSpec attachDetachSpec;
    InputStream is;
    try {
      List<PriceComponent> priceComponents = PriceComponent.findByProvider(provider);

      List<CertificateInfo> certificateInfoList = CertificateInfo.getCertificateInfoList(universe);

      List<KmsHistory> kmsHistoryList =
          EncryptionAtRestUtil.getAllUniverseKeys(universe.getUniverseUUID());
      kmsHistoryList.sort((h1, h2) -> h1.getTimestamp().compareTo(h2.getTimestamp()));
      List<KmsConfig> kmsConfigs =
          kmsHistoryList.stream()
              .map(KmsHistory::getConfigUuid)
              .distinct()
              .map(KmsConfig::get)
              .collect(Collectors.toList());

      List<Backup> backups =
          Backup.fetchByUniverseUUID(customer.getUuid(), universe.getUniverseUUID());
      List<Schedule> schedules =
          Schedule.getAllSchedulesByOwnerUUIDAndType(
              universe.getUniverseUUID(), TaskType.CreateBackup);
      List<CustomerConfig> customerConfigs =
          backups.stream()
              .map(Backup::getStorageConfigUUID)
              .distinct()
              .map(CustomerConfig::get)
              .collect(Collectors.toList());

      // Non-local releases will not be populated by importLocalReleases, so we need to add it
      // ourselves.
      ReleaseContainer release =
          releaseManager.getReleaseByVersion(
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      if (release != null && release.hasLocalRelease()) {
        release = null;
      }
      if (release != null) {
        release.setArtifactMatchingArchitecture(universe.getUniverseDetails().arch);
      }

      List<NodeInstance> nodeInstances = NodeInstance.listByUniverse(universe.getUniverseUUID());

      String storagePath = AppConfigHelper.getStoragePath();
      String releasesPath = confGetter.getStaticConf().getString(RELEASES_PATH);
      String ybcReleasePath = confGetter.getStaticConf().getString(YBC_RELEASE_PATH);
      String ybcReleasesPath = confGetter.getStaticConf().getString(YBC_RELEASES_PATH);

      PlatformPaths platformPaths =
          PlatformPaths.builder()
              .storagePath(storagePath)
              .releasesPath(releasesPath)
              .ybcReleasePath(ybcReleasePath)
              .ybcReleasesPath(ybcReleasesPath)
              .build();

      attachDetachSpec =
          AttachDetachSpec.builder()
              .universe(universe)
              .universeConfig(universe.getConfig())
              .provider(provider)
              .instanceTypes(instanceTypes)
              .priceComponents(priceComponents)
              .certificateInfoList(certificateInfoList)
              .nodeInstances(nodeInstances)
              .kmsHistoryList(kmsHistoryList)
              .kmsConfigs(kmsConfigs)
              .schedules(schedules)
              .backups(backups)
              .customerConfigs(customerConfigs)
              .ybReleaseMetadata(release != null ? release.toImportExportRelease() : null)
              .oldPlatformPaths(platformPaths)
              .skipReleases(detachUniverseSpec.getSkipReleases())
              .build();

      is = attachDetachSpec.exportSpec();
    } catch (Exception e) {
      // Unlock the universe if error is thrown to return universe back to original state.
      Util.unlockUniverse(universe);
      throw e;
    }
    return is;
  }

  public void attachUniverse(
      Request request, UUID customerUUID, UUID universeUUID, AttachUniverseSpec attachUniverseSpec)
      throws IOException {
    checkAttachDetachEnabled();
    Customer customer = Customer.getOrBadRequest(customerUUID);

    if (attachUniverseSpec == null || attachUniverseSpec.getDownloadedSpecFile() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Failed to get downloaded spec file");
    }

    if (Universe.maybeGet(universeUUID).isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Universe with uuid %s already exists", universeUUID.toString()));
    }

    String storagePath = AppConfigHelper.getStoragePath();
    String releasesPath = confGetter.getStaticConf().getString(RELEASES_PATH);
    String ybcReleasePath = confGetter.getStaticConf().getString(YBC_RELEASE_PATH);
    String ybcReleasesPath = confGetter.getStaticConf().getString(YBC_RELEASES_PATH);

    PlatformPaths platformPaths =
        PlatformPaths.builder()
            .storagePath(storagePath)
            .releasesPath(releasesPath)
            .ybcReleasePath(ybcReleasePath)
            .ybcReleasesPath(ybcReleasesPath)
            .build();

    AttachDetachSpec attachDetachSpec =
        AttachDetachSpec.importSpec(
            attachUniverseSpec.getDownloadedSpecFile().getRef().path(), platformPaths, customer);
    attachDetachSpec.save(platformPaths, releaseManager, swamperHelper);
  }

  @Transactional
  public void deleteAttachDetachMetadata(Request request, UUID customerUUID, UUID universeUUID)
      throws IOException {
    checkAttachDetachEnabled();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    List<Schedule> schedules =
        Schedule.getAllSchedulesByOwnerUUIDAndType(
            universe.getUniverseUUID(), TaskType.CreateBackup);

    for (Schedule schedule : schedules) {
      log.info("Deleting schedule: {}... of universe: {}", schedule, universeUUID);
      schedule.delete();
    }

    List<NodeInstance> nodeInstances = NodeInstance.listByUniverse(universe.getUniverseUUID());
    for (NodeInstance nodeInstance : nodeInstances) {
      log.info("Deleting node instance: {}", nodeInstance);
      nodeInstance.delete();
    }
    Universe.delete(universe.getUniverseUUID());
  }

  private void checkAttachDetachEnabled() {
    boolean attachDetachEnabled = confGetter.getGlobalConf(GlobalConfKeys.attachDetachEnabled);
    if (!attachDetachEnabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Attach/Detach feature is not enabled. Please set the runtime flag"
              + " 'yb.attach_detach.enabled' to true.");
    }
  }
}
