// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.METHOD_NOT_ALLOWED;

import api.v2.mappers.ClusterMapper;
import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.mappers.UniverseResourceDetailsMapper;
import api.v2.mappers.UniverseRespMapper;
import api.v2.models.AttachUniverseSpec;
import api.v2.models.ClusterAddSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.ClusterSpec.ClusterTypeEnum;
import api.v2.models.DetachUniverseSpec;
import api.v2.models.ExecutionSummary;
import api.v2.models.NodeScriptResult;
import api.v2.models.NodeSelection;
import api.v2.models.RunScriptRequest;
import api.v2.models.RunScriptResponse;
import api.v2.models.ScriptOptions;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseDeleteSpec;
import api.v2.models.UniverseEditSpec;
import api.v2.models.UniverseOperatorImportReq;
import api.v2.models.UniverseSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.OperatorImportUniverse;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.LocalhostAccessChecker;
import com.yugabyte.yw.common.NodeScriptRunner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AttachDetachSpec;
import com.yugabyte.yw.models.AttachDetachSpec.PlatformPaths;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import io.ebean.annotation.Transactional;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import play.mvc.Http.Request;

@Slf4j
public class UniverseManagementHandler extends ApiControllerUtils {
  @Inject private RuntimeConfGetter confGetter;
  @Inject private ReleaseManager releaseManager;
  @Inject private SwamperHelper swamperHelper;
  @Inject private ConfigHelper configHelper;
  @Inject private UniverseCRUDHandler universeCRUDHandler;
  @Inject private UniverseInfoHandler universeInfoHandler;
  @Inject private Commissioner commissioner;
  @Inject private YsqlQueryExecutor ysqlQueryExecutor;
  @Inject private NodeScriptRunner nodeScriptRunner;
  @Inject private LocalhostAccessChecker localhostChecker;

  private static final String RELEASES_PATH = "yb.releases.path";

  /** Default max script file size for audit logging (1 MB) */
  private static final long DEFAULT_MAX_SCRIPT_FILE_SIZE_BYTES = 1024 * 1024;

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
    v1Params.rootCA = universeCRUDHandler.checkValidRootCA(dbUniverse.getUniverseDetails().rootCA);
    UUID taskUUID = commissioner.submit(taskType, v1Params);
    log.info(
        "Submitted {} for {} : {}, task uuid = {}.",
        taskType,
        uniUUID,
        dbUniverse.getName(),
        taskUUID);
    CustomerTask.create(
        customer,
        dbUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Update,
        dbUniverse.getName(),
        CustomerTaskManager.getCustomTaskName(CustomerTask.TaskType.Update, v1Params, null));
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
    Util.validateUniverseOwnershipAndNotDetached(
        universe, configHelper, ysqlQueryExecutor, confGetter);

    // Validate DB version supports attach/detach
    String dbVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    validateUniverseVersionForAttachDetach(dbVersion);

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

    // TODO: Replace this with lockAndFreezeUniverseForUpdate
    universe = Util.lockUniverse(universe);
    AttachDetachSpec attachDetachSpec;
    InputStream is;
    try {
      List<PriceComponent> priceComponents = PriceComponent.findByProvider(provider);

      Map<UUID, ImageBundle> imageBundles = new HashMap<>();
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      for (UniverseDefinitionTaskParams.Cluster cluster : universeDetails.clusters) {
        UUID imageBundleUUID = cluster.userIntent.imageBundleUUID;
        if (imageBundleUUID != null) {
          ImageBundle imageBundle = ImageBundle.get(imageBundleUUID);
          if (imageBundle != null && !imageBundles.containsKey(imageBundleUUID)) {
            imageBundles.put(imageBundleUUID, imageBundle);
          }
        }
      }
      if (imageBundles.isEmpty()) {
        UUID providerUUID =
            UUID.fromString(universeDetails.getPrimaryCluster().userIntent.provider);
        List<ImageBundle> defaultBundles = ImageBundle.getDefaultForProvider(providerUUID);
        for (ImageBundle defaultBundle : defaultBundles) {
          imageBundles.put(defaultBundle.getUuid(), defaultBundle);
        }
      }

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

      PlatformPaths platformPaths =
          PlatformPaths.builder().storagePath(storagePath).releasesPath(releasesPath).build();

      attachDetachSpec =
          AttachDetachSpec.builder()
              .universe(universe)
              .universeConfig(universe.getConfig())
              .provider(provider)
              .instanceTypes(instanceTypes)
              .priceComponents(priceComponents)
              .imageBundles(imageBundles)
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
      detachDbFromYBA(universe);
      setUniverseDetachedState(universe, true);
    } catch (Exception e) {
      // Unlock the universe if error is thrown to return universe back to original state.
      // TODO: Replace this with unlockUniverseForUpdate
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

    PlatformPaths platformPaths =
        PlatformPaths.builder().storagePath(storagePath).releasesPath(releasesPath).build();

    AttachDetachSpec attachDetachSpec =
        AttachDetachSpec.importSpec(
            attachUniverseSpec.getDownloadedSpecFile().getRef().path(), platformPaths, customer);

    // Validate DB version supports attach/detach
    String dbVersion =
        attachDetachSpec
            .getUniverse()
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .ybSoftwareVersion;
    validateUniverseVersionForAttachDetach(dbVersion);

    validateProviderCompatibility(attachDetachSpec.getProvider());
    // Software (platform) version of dest, check with source version. Should be same
    String destVersion =
        StringUtils.substringBefore(
            String.valueOf(
                configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion).get("version")),
            "-");
    String srcVersion =
        StringUtils.substringBefore(
            attachDetachSpec.getUniverse().getUniverseDetails().getPlatformVersion(), "-");
    if (!srcVersion.equalsIgnoreCase(destVersion)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Software versions do not match, please attach to a platform with software version: "
              + srcVersion);
    }
    attachDetachSpec.save(
        platformPaths, releaseManager, swamperHelper, configHelper, ysqlQueryExecutor, confGetter);
  }

  @Transactional
  public void deleteAttachDetachMetadata(Request request, UUID customerUUID, UUID universeUUID)
      throws IOException {
    checkAttachDetachEnabled();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    // Validate DB version supports attach/detach
    String dbVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    validateUniverseVersionForAttachDetach(dbVersion);

    // Delete metadata is allowed if universe is detached, or if this YBA instance is not the
    // universe owner (i.e. universe is attached elsewhere)
    if (universe.getUniverseDetails().universeDetached
        || !Util.isUniverseOwner(universe, configHelper, ysqlQueryExecutor, confGetter)) {
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
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot delete metadata for a universe that is owned by this YBA instance and is in a"
              + " healthy state. The universe must be detached or owned by another YBA"
              + " to allow metadata deletion.");
    }
  }

  @Transactional
  public void rollbackDetachUniverse(
      Request request, UUID customerUUID, UUID universeUUID, Boolean isForceRollback)
      throws IOException {
    checkAttachDetachEnabled();
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    String dbVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    validateUniverseVersionForAttachDetach(dbVersion);

    UUID currentYwUuid = configHelper.getYugawareUUID();
    if (currentYwUuid == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Current YugabyteDB Anywhere UUID not found");
    }

    UUID storedYwUuid = Util.getStoredYwUuid(universe, ysqlQueryExecutor, confGetter);

    boolean forceRollback = Boolean.TRUE.equals(isForceRollback);

    if (!forceRollback) {
      if (!universe.getUniverseDetails().universeDetached) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Universe is not in detached state. Cannot rollback detach for a universe that is not"
                + " detached. Use isForceRollback=true to override this check.");
      }

      if (storedYwUuid == null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Cannot determine universe owner (YSQL not available). Since ownership cannot be"
                + " checked, this operation could allow multiple YBAs to control same universe."
                + " Proceed with caution and use isForceRollback=true to rollback anyway.");
      }

      if (!storedYwUuid.equals(AttachDetachSpec.DETACHED_UNIVERSE_UUID)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Universe owner is not DETACHED_UNIVERSE_UUID (current owner: %s). "
                    + "Cannot rollback detach for a universe that already has an owner. "
                    + "Use isForceRollback=true to override this check.",
                storedYwUuid));
      }
    }

    log.info(
        "Rolling back detach for universe {} (isForceRollback={}). Resetting universeDetached to"
            + " false and updating owner to {}",
        universe.getName(),
        forceRollback,
        currentYwUuid);

    try {
      universe = setUniverseDetachedState(universe, false);

      if (storedYwUuid != null) {
        updateYwUuidInConsistencyTable(universe, currentYwUuid, Util.getYwHostnameOrIP());
      }

      log.info("Successfully rolled back detach for universe {}", universe.getName());
    } finally {
      universe = Util.unlockUniverse(universe);
    }
  }

  private void validateProviderCompatibility(Provider sourceProvider) {
    if (sourceProvider.getCloudCode() == Common.CloudType.kubernetes) {
      if (!KubernetesEnvironmentVariables.isYbaRunningInKubernetes()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Cannot attach Kubernetes universe to this YBA. Kubernetes universes can only be"
                + " attached to Kubernetes-based YBA installations.");
      }

      if (isAutoProvider(sourceProvider)) {
        boolean allowAutoProviderToK8sPlatform =
            confGetter.getGlobalConf(GlobalConfKeys.allowAutoProviderToK8sPlatform);

        if (!allowAutoProviderToK8sPlatform) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Cannot attach Kubernetes universe with auto-provider to this YBA. To allow this"
                  + " operation, set the runtime flag"
                  + " 'yb.attach_detach.allow_auto_provider_to_k8s_platform' to true. Note that"
                  + " you must only attach auto-provider universe to Kubernetes-based YBA if the"
                  + " destination and source YBA exist on the same Kubernetes cluster");
        }
      }
    }
  }

  // Determine if provider is auto-configured (k8s only). Auto providers automatically inherit their
  // configuration from the K8s cluster where YBA is deployed. Hence, auto providers should lack a
  // KUBECONFIG
  private boolean isAutoProvider(Provider provider) {
    return provider.getCloudCode() == Common.CloudType.kubernetes && !hasKubeConfig(provider);
  }

  private boolean hasKubeConfig(Provider provider) {
    Map<String, String> providerConfig = CloudInfoInterface.fetchEnvVars(provider);
    KubernetesInfo kubernetesInfo = CloudInfoInterface.get(provider);

    return providerConfig.containsKey("KUBECONFIG_NAME") || kubernetesInfo.getKubeConfig() != null;
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

  private void validateUniverseVersionForAttachDetach(String dbVersion) {
    if (Util.compareYBVersions(
            dbVersion, "2024.2.0.0-b1", "2.23.1.0-b29", true /* suppressFormatError */)
        < 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Universe DB version %s does not support attach/detach operations. "
                  + "Minimum required version is 2024.2.0.0-b1 (stable) or 2.23.1.0-b29 (preview).",
              dbVersion));
    }
  }

  private Universe setUniverseDetachedState(Universe universe, boolean detached) {
    UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
          universeDetails.universeDetached = detached;
          u.setUniverseDetails(universeDetails);
        };
    return Universe.saveDetails(universe.getUniverseUUID(), updater, false);
  }

  private void detachDbFromYBA(Universe universe) {
    updateYwUuidInConsistencyTable(
        universe, AttachDetachSpec.DETACHED_UNIVERSE_UUID, AttachDetachSpec.DETACHED_HOST);
  }

  private void updateYwUuidInConsistencyTable(
      Universe universe, UUID targetYwUuid, String targetYwHost) {
    try {
      NodeDetails node = CommonUtils.getServerToRunYsqlQuery(universe, true);

      String updateQuery =
          String.format(
              "UPDATE %s SET yw_uuid = '%s', yw_host = '%s' WHERE seq_num = (SELECT MAX(seq_num)"
                  + " FROM %s)",
              Util.CONSISTENCY_CHECK_TABLE_NAME,
              targetYwUuid.toString(),
              targetYwHost,
              Util.CONSISTENCY_CHECK_TABLE_NAME);

      RunQueryFormData runQueryFormData = new RunQueryFormData();
      runQueryFormData.setDbName(Util.SYSTEM_PLATFORM_DB);
      runQueryFormData.setQuery(updateQuery);

      JsonNode ysqlResponse =
          ysqlQueryExecutor.executeQueryInNodeShell(
              universe,
              runQueryFormData,
              node,
              confGetter.getConfForScope(universe, UniverseConfKeys.ysqlConsistencyTimeoutSecs));

      if (ysqlResponse != null && ysqlResponse.has("error")) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format(
                "Error while updating YW UUID in consistency check table: %s",
                ysqlResponse.get("error").asText()));
      }
      log.info(
          "Updated YW UUID to {} and YW host to '{}' in consistency check table for universe: {}",
          targetYwUuid,
          targetYwHost,
          universe.getName());
    } catch (IllegalStateException e) {
      log.error(
          "Failed to find valid tserver to update YW UUID for universe {}: {}",
          universe.getName(),
          e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Failed to find valid tserver to update YW UUID for universe %s: %s",
              universe.getName(), e.getMessage()));
    } catch (Exception e) {
      log.error("Error updating YW UUID for universe {}: {}", universe.getName(), e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Failed to update YW UUID for universe %s: %s", universe.getName(), e.getMessage()));
    }
  }

  public api.v2.models.UniverseResourceDetails getUniverseResources(
      Request request, UUID cUUID, UniverseCreateSpec universeSpec) throws JsonProcessingException {
    Customer customer = Customer.getOrBadRequest(cUUID);
    log.info("Get Universe resource details with v2 spec: {}", prettyPrint(universeSpec));
    // map universeSpec to v1 universe details
    UniverseDefinitionTaskParams v1DefnParams =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV1UniverseDefinitionTaskParamsFromCreateSpec(
            universeSpec);
    log.debug("Get Universe resource details translated to v1 spec: {}", prettyPrint(v1DefnParams));
    UniverseConfigureTaskParams v1Params =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toUniverseConfigureTaskParams(
            v1DefnParams, request);

    v1Params.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    v1Params.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, v1Params);

    if (v1Params.clusters.stream().anyMatch(cluster -> cluster.clusterType == ClusterType.ASYNC)) {
      v1Params.currentClusterType = ClusterType.ASYNC;
      universeCRUDHandler.configure(customer, v1Params);
    }

    v1DefnParams.nodeDetailsSet = v1Params.nodeDetailsSet;

    UniverseResourceDetails v1UniverseResourceDetails =
        universeInfoHandler.getUniverseResources(customer, v1DefnParams);
    // map to v2 UniverseResourceDetails
    api.v2.models.UniverseResourceDetails v2Response =
        UniverseResourceDetailsMapper.INSTANCE.toV2UniverseResourceDetails(
            v1UniverseResourceDetails);
    if (log.isTraceEnabled()) {
      log.trace("Got Universe resource details {}", prettyPrint(v2Response));
    }
    return v2Response;
  }

  public void precheckOperatorImportUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseOperatorImportReq req) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    // validate the universe is kubernetes
    if (!Util.isKubernetesBasedUniverse(universe)) {
      log.error(
          "Universe {} is not a Kubernetes universe, cannot migrate to operator",
          universe.getName());
      throw new PlatformServiceException(BAD_REQUEST, "Universe is not a Kubernetes universe");
    }
    if (!confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
      log.error("Operator is not enabled, cannot migrate universe {}", universe.getName());
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Operator is not enabled. Please enable the runtime config"
              + " 'yb.kubernetes.operator.enabled' and restart YBA");
    }

    // Check if the namespace is the same as the operator namespace if it is set
    boolean migrateNamespaceSet = req.getNamespace() != null && !req.getNamespace().isEmpty();
    String operatorNamespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    boolean operatorNamespaceSet = operatorNamespace != null && !operatorNamespace.isEmpty();
    if (migrateNamespaceSet
        && operatorNamespaceSet
        && !req.getNamespace().equals(operatorNamespace)) {
      log.error(
          "Namespace {} is not the same as the operator namespace {}",
          req.getNamespace(),
          operatorNamespace);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Namespace is not the same as the operator namespace. Please set the namespace to the"
              + " same as the operator namespace");
    }

    // XCluster is not supported by operator
    if (!XClusterConfig.getByUniverseUuid(universe.getUniverseUUID()).isEmpty()) {
      log.error("Universe {} has xClusterInfo set, cannot migrate to operator", universe.getName());
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot migrate universes in an xcluster setup.");
    }

    // AZ Level overrides are not supported by operator
    Map<String, String> azOverrides =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.azOverrides;
    if (azOverrides != null && azOverrides.size() > 0) {
      log.error(
          "Universe {} has AZ level overrides set, cannot migrate to operator", universe.getName());
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot migrate universes with AZ level overrides.");
    }
    log.info("Universe {} precheck for operator import success", universe.getName());
  }

  public YBATask operatorImportUniverse(
      Request request, UUID cUUID, UUID uniUUID, UniverseOperatorImportReq req) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    precheckOperatorImportUniverse(request, cUUID, uniUUID, req);
    OperatorImportUniverse.Params params = new OperatorImportUniverse.Params();
    params.setUniverseUUID(uniUUID);
    params.namespace = req.getNamespace();
    UUID taskUuid = commissioner.submit(TaskType.OperatorImportUniverse, params);
    CustomerTask.create(
        customer,
        uniUUID,
        taskUuid,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.OperatorImport,
        universe.getName());
    YBATask ybaTask = new YBATask().taskUuid(taskUuid).resourceUuid(universe.getUniverseUUID());
    return ybaTask;
  }

  /**
   * Runs a script on selected nodes in a universe and returns the results.
   *
   * @param request The HTTP request
   * @param cUUID Customer UUID
   * @param uniUUID Universe UUID
   * @param runScriptRequest The request containing script options and node selection
   * @return RunScriptResponse with execution results from all targeted nodes
   */
  public RunScriptResponse runScript(
      Request request, UUID cUUID, UUID uniUUID, RunScriptRequest runScriptRequest) {
    localhostChecker.checkLocalhost(request);
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);

    // Check if the feature is enabled
    boolean nodeScriptEnabled =
        confGetter.getConfForScope(universe, UniverseConfKeys.nodeScriptEnabled);
    if (!nodeScriptEnabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Node script execution API is not enabled for this universe. "
              + "Please set the runtime config 'yb.node_script.enabled' to true.");
    }

    // Validate request
    ScriptOptions scriptOptions = runScriptRequest.getScriptOptions();
    if (scriptOptions == null) {
      throw new PlatformServiceException(BAD_REQUEST, "script_options is required");
    }

    String scriptContent = scriptOptions.getScriptContent();
    String scriptFile = scriptOptions.getScriptFile();

    if (StringUtils.isBlank(scriptContent) && StringUtils.isBlank(scriptFile)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Either script_content or script_file must be provided");
    }

    if (StringUtils.isNotBlank(scriptContent) && StringUtils.isNotBlank(scriptFile)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Only one of script_content or script_file should be provided");
    }

    // If script_file is provided, validate it exists and read the content for auditing
    String scriptFileContents = null;
    if (StringUtils.isNotBlank(scriptFile)) {
      File file = new File(scriptFile);
      if (!file.exists()) {
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("Script file not found: %s", scriptFile));
      }
      if (!file.canRead()) {
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("Script file is not readable: %s", scriptFile));
      }
      double fileSizeMB = file.length() / (1024.0 * 1024.0);
      log.info("Script file {} size: {} MB", scriptFile, String.format("%.2f", fileSizeMB));
      // Limit file size to avoid memory issues during auditing (default 1MB)
      long maxFileSizeBytes =
          scriptOptions.getMaxScriptFileSizeBytes() != null
              ? scriptOptions.getMaxScriptFileSizeBytes()
              : DEFAULT_MAX_SCRIPT_FILE_SIZE_BYTES;
      if (file.length() > maxFileSizeBytes) {
        log.warn(
            "Script file {} exceeds max size ({} bytes), skipping content capture for auditing",
            scriptFile,
            maxFileSizeBytes);
      } else {
        try {
          scriptFileContents = Files.readString(file.toPath());
        } catch (Exception e) {
          throw new PlatformServiceException(
              BAD_REQUEST, String.format("Failed to read script file: %s", scriptFile));
        }
      }
    }

    // Build script params
    long timeoutSecs =
        scriptOptions.getTimeoutSecs() != null ? scriptOptions.getTimeoutSecs() : 60L;
    String linuxUser = scriptOptions.getLinuxUser();
    NodeScriptRunner.ScriptParams scriptParams =
        NodeScriptRunner.ScriptParams.builder()
            .scriptContent(scriptContent)
            .scriptFile(scriptFile)
            .params(scriptOptions.getParams())
            .timeoutSecs(timeoutSecs)
            .linuxUser(linuxUser)
            .build();

    // Build node filter
    NodeScriptRunner.NodeFilter nodeFilter = null;
    NodeSelection nodeSelection = runScriptRequest.getNodes();
    if (nodeSelection != null) {
      int maxParallelNodes =
          nodeSelection.getMaxParallelNodes() != null ? nodeSelection.getMaxParallelNodes() : 50;
      nodeFilter =
          NodeScriptRunner.NodeFilter.builder()
              .nodeNames(nodeSelection.getNodeNames())
              .clusterUuid(nodeSelection.getClusterUuid())
              .mastersOnly(nodeSelection.getMastersOnly())
              .tserversOnly(nodeSelection.getTserversOnly())
              .maxParallelNodes(maxParallelNodes)
              .build();
    }

    // Execute via service
    NodeScriptRunner.ExecutionResult result =
        nodeScriptRunner.runScript(universe, scriptParams, nodeFilter);

    if (result.getTotalNodes() == 0) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No nodes found matching the selection criteria");
    }

    // Map to API response
    Map<String, NodeScriptResult> nodeResults = new LinkedHashMap<>();
    for (Map.Entry<String, NodeScriptRunner.NodeResult> entry :
        result.getNodeResults().entrySet()) {
      NodeScriptRunner.NodeResult nr = entry.getValue();
      nodeResults.put(
          entry.getKey(),
          new NodeScriptResult()
              .nodeName(nr.getNodeName())
              .nodeAddress(nr.getNodeAddress())
              .exitCode(nr.getExitCode())
              .stdout(nr.getStdout())
              .executionTimeMs(nr.getExecutionTimeMs())
              .success(nr.isSuccess())
              .errorMessage(nr.getErrorMessage()));
    }

    ExecutionSummary summary =
        new ExecutionSummary()
            .totalNodes(result.getTotalNodes())
            .successfulNodes(result.getSuccessfulNodes())
            .failedNodes(result.getFailedNodes())
            .totalExecutionTimeMs(result.getTotalExecutionTimeMs())
            .allSucceeded(result.isAllSucceeded());

    // Create audit entry with the script details including file contents if applicable
    JsonNode additionalDetails = null;
    if (scriptFileContents != null) {
      additionalDetails = Json.newObject().put("script_file_contents", scriptFileContents);
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            uniUUID.toString(),
            Audit.ActionType.RunScript,
            Json.toJson(runScriptRequest),
            null /* taskUUID - this is a synchronous operation */,
            additionalDetails);

    return new RunScriptResponse().summary(summary).results(nodeResults);
  }
}
