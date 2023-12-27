// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.backuprestore.ybc;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.PublicCloudConstants.OsType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.NFSUtil;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.YbcThrottleParameters;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse.PresetThrottleValues;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceNfsDirDeleteRequest;
import org.yb.ybc.BackupServiceNfsDirDeleteResponse;
import org.yb.ybc.BackupServiceTaskAbortRequest;
import org.yb.ybc.BackupServiceTaskAbortResponse;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskDeleteRequest;
import org.yb.ybc.BackupServiceTaskDeleteResponse;
import org.yb.ybc.BackupServiceTaskEnabledFeaturesRequest;
import org.yb.ybc.BackupServiceTaskEnabledFeaturesResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.BackupServiceTaskThrottleParametersGetRequest;
import org.yb.ybc.BackupServiceTaskThrottleParametersGetResponse;
import org.yb.ybc.BackupServiceTaskThrottleParametersSetRequest;
import org.yb.ybc.BackupServiceTaskThrottleParametersSetResponse;
import org.yb.ybc.ControllerObjectTaskThrottleParameters;
import org.yb.ybc.ControllerStatus;
import org.yb.ybc.PingRequest;
import org.yb.ybc.PingResponse;

@Singleton
public class YbcManager {

  private static final Logger LOG = LoggerFactory.getLogger(YbcManager.class);

  private final YbcClientService ybcClientService;
  private final CustomerConfigService customerConfigService;
  private final RuntimeConfGetter confGetter;
  private final ReleaseManager releaseManager;
  private final NodeManager nodeManager;
  private final KubernetesManagerFactory kubernetesManagerFactory;
  private final StorageUtilFactory storageUtilFactory;

  private static final int WAIT_EACH_ATTEMPT_MS = 5000;
  private static final int WAIT_EACH_SHORT_ATTEMPT_MS = 2000;
  private static final int MAX_RETRIES = 10;
  private static final int MAX_NUM_RETRIES = 20;
  private static final long INITIAL_SLEEP_TIME_IN_MS = 1000L;
  private static final long INCREMENTAL_SLEEP_TIME_IN_MS = 2000L;

  @Inject
  public YbcManager(
      YbcClientService ybcClientService,
      CustomerConfigService customerConfigService,
      RuntimeConfGetter confGetter,
      ReleaseManager releaseManager,
      NodeManager nodeManager,
      KubernetesManagerFactory kubernetesManagerFactory,
      StorageUtilFactory storageUtilFactory) {
    this.ybcClientService = ybcClientService;
    this.customerConfigService = customerConfigService;
    this.confGetter = confGetter;
    this.releaseManager = releaseManager;
    this.nodeManager = nodeManager;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
    this.storageUtilFactory = storageUtilFactory;
  }

  // Enum for YBC throttle param type.
  private enum ThrottleParamType {
    // List containing the throttle params for this param type.
    CONCURRENCY_PARAM(
        GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS, GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS),
    NUM_OBJECTS_PARAM(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS, GFlagsUtil.YBC_PER_UPLOAD_OBJECTS);

    private final String[] throttleParamsTypeFlag;

    ThrottleParamType(String... throttleParamsTypeFlag) {
      this.throttleParamsTypeFlag = throttleParamsTypeFlag;
    }

    public ImmutableSet<String> throttleTypeFlags() {
      return ImmutableSet.copyOf(throttleParamsTypeFlag);
    }

    public static ThrottleParamType throttleFlagType(String throttleFlag) {
      return Arrays.stream(ThrottleParamType.values())
          .filter(tP -> tP.throttleTypeFlags().contains(throttleFlag))
          .findFirst()
          .get();
    }
  }

  private int capThrottleValue(String throttleParamName, int throttleParamValue, int numCores) {
    return Math.min(
        throttleParamValue,
        getPresetThrottleValues(numCores, ThrottleParamType.throttleFlagType(throttleParamName))
            .getMaxValue());
  }

  // Provides default values for throttle params based on throttle param type.
  private PresetThrottleValues getPresetThrottleValues(
      int ceilNumCores, ThrottleParamType paramType) {
    switch (paramType) {
      case CONCURRENCY_PARAM:
        return new PresetThrottleValues(2, 1, ceilNumCores + 1);
      case NUM_OBJECTS_PARAM:
        return new PresetThrottleValues(ceilNumCores / 2 + 1, 1, ceilNumCores + 1);
      default:
        throw new RuntimeException("Unknown throttle param type");
    }
  }

  public void setThrottleParams(UUID universeUUID, YbcThrottleParameters throttleParams) {
    try {
      BackupServiceTaskThrottleParametersSetRequest.Builder throttleParamsSetterBuilder =
          BackupServiceTaskThrottleParametersSetRequest.newBuilder();
      throttleParamsSetterBuilder.setPersistAcrossReboots(true);

      Universe universe = Universe.getOrBadRequest(universeUUID);

      Map<UUID, Map<String, String>> clusterYbcFlagsMap = new HashMap<>();

      // Stream through clusters to populate and set throttle param values.
      universe.getUniverseDetails().clusters.stream()
          .forEach(
              c -> {
                ControllerObjectTaskThrottleParameters.Builder controllerObjectThrottleParams =
                    ControllerObjectTaskThrottleParameters.newBuilder();
                List<String> toRemove = new ArrayList<>();
                Map<String, String> toAddModify = new HashMap<>();
                Map<String, Integer> paramsToSet = throttleParams.getThrottleFlagsMap();
                List<NodeDetails> tsNodes =
                    universe.getNodesInCluster(c.uuid).stream()
                        .filter(nD -> nD.isTserver)
                        .collect(Collectors.toList());
                if (throttleParams.resetDefaults) {
                  // Nothing required to do for controllerObjectThrottleParams,
                  // empty object sets default values on YB-Controller.
                  toRemove.addAll(new ArrayList<>(paramsToSet.keySet()));
                } else {

                  // Populate the controller throttle params map with modified throttle param
                  // values.
                  populateControllerThrottleParamsMap(
                      universe,
                      tsNodes,
                      toAddModify,
                      c,
                      controllerObjectThrottleParams,
                      paramsToSet);

                  throttleParamsSetterBuilder.setParams(controllerObjectThrottleParams.build());
                }
                BackupServiceTaskThrottleParametersSetRequest throttleParametersSetRequest =
                    throttleParamsSetterBuilder.build();

                // On node by node basis set the throttle params.
                setThrottleParamsOnYbcServers(
                    universe, tsNodes, c.uuid, throttleParametersSetRequest);

                Map<String, String> currentYbcFlagsMap = new HashMap<>(c.userIntent.ybcFlags);
                currentYbcFlagsMap.putAll(toAddModify);
                currentYbcFlagsMap.keySet().removeAll(toRemove);
                clusterYbcFlagsMap.put(c.uuid, currentYbcFlagsMap);
              });

      // Update universe details with modified values.
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams params = universe.getUniverseDetails();
              params.clusters.forEach(
                  c -> {
                    c.userIntent.ybcFlags = clusterYbcFlagsMap.get(c.uuid);
                  });
              universe.setUniverseDetails(params);
            }
          };
      Universe.saveDetails(universeUUID, updater, false);
    } catch (Exception e) {
      LOG.info(
          "Setting throttle params for universe {} failed with: {}", universeUUID, e.getMessage());
      throw new RuntimeException(e.getMessage());
    }
  }

  private void populateControllerThrottleParamsMap(
      Universe universe,
      List<NodeDetails> tsNodes,
      Map<String, String> toAddModify,
      Cluster c,
      ControllerObjectTaskThrottleParameters.Builder controllerObjectThrottleParams,
      Map<String, Integer> paramsToSet) {
    Integer ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    String certFile = universe.getCertificateNodetoNode();
    UUID providerUUID = UUID.fromString(c.userIntent.provider);
    List<String> tsIPs =
        tsNodes.parallelStream().map(nD -> nD.cloudInfo.private_ip).collect(Collectors.toList());

    YbcClient ybcClient = null;
    Map<FieldDescriptor, Object> currentThrottleParamsMap = null;
    // Get already present throttle values on YBC.
    try {
      ybcClient = getYbcClient(tsIPs, ybcPort, certFile);
      currentThrottleParamsMap = getThrottleParamsAsFieldDescriptor(ybcClient);
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
    if (MapUtils.isEmpty(currentThrottleParamsMap)) {
      throw new RuntimeException("Got empty map for current throttle params from YB-Controller");
    }
    int cInstanceTypeCores =
        (int)
            Math.ceil(
                InstanceType.getOrBadRequest(providerUUID, tsNodes.get(0).cloudInfo.instance_type)
                    .getNumCores());

    // Modify throttle params map as need be, according to new values. Also cap the values
    // to instance type maximum allowed.
    currentThrottleParamsMap.forEach(
        (k, v) -> {
          int providedThrottleValue = paramsToSet.get(k.getName());
          if (providedThrottleValue > 0) {
            // Validate here
            int throttleParamValue =
                capThrottleValue(k.getName(), providedThrottleValue, cInstanceTypeCores);
            if (throttleParamValue < providedThrottleValue) {
              LOG.info(
                  "Provided throttle param: {} value: {} is more than max allowed,"
                      + " for cluster: {}, capping values to {}",
                  k.getName(),
                  providedThrottleValue,
                  c.uuid,
                  throttleParamValue);
            }
            controllerObjectThrottleParams.setField(k, throttleParamValue);
            toAddModify.put(k.getName(), Integer.toString(throttleParamValue));
          } else {
            controllerObjectThrottleParams.setField(k, v);
          }
        });
  }

  // Iterate through each node and set throttle values.
  private void setThrottleParamsOnYbcServers(
      Universe universe,
      List<NodeDetails> tsNodes,
      UUID clusterUUID,
      BackupServiceTaskThrottleParametersSetRequest throttleParametersSetRequest) {
    Integer ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    String certFile = universe.getCertificateNodetoNode();
    tsNodes.forEach(
        (n) -> {
          YbcClient client = null;
          try {
            String nodeIp = n.cloudInfo.private_ip;
            if (nodeIp == null || !n.isTserver) {
              return;
            }
            client = ybcClientService.getNewClient(nodeIp, ybcPort, certFile);
            BackupServiceTaskThrottleParametersSetResponse throttleParamsSetResponse =
                client.backupServiceTaskThrottleParametersSet(throttleParametersSetRequest);
            if (throttleParamsSetResponse != null
                && !throttleParamsSetResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
              throw new RuntimeException(
                  String.format(
                      "Failed to set throttle params on node %s universe %s with error: %s",
                      nodeIp,
                      universe.getUniverseUUID().toString(),
                      throttleParamsSetResponse.getStatus()));
            }
          } finally {
            ybcClientService.closeClient(client);
          }
        });
  }

  public YbcThrottleParametersResponse getThrottleParams(UUID universeUUID) {
    YbcClient ybcClient = null;
    try {
      ybcClient = getYbcClient(universeUUID);
      Map<String, Integer> currentThrottleParamMap =
          getThrottleParamsAsFieldDescriptor(ybcClient).entrySet().stream()
              .collect(Collectors.toMap(k -> k.getKey().getName(), v -> (int) v.getValue()));

      YbcThrottleParametersResponse throttleParamsResponse = new YbcThrottleParametersResponse();
      throttleParamsResponse.setThrottleParamsMap(
          getThrottleParamsMap(universeUUID, currentThrottleParamMap));
      return throttleParamsResponse;
    } catch (Exception e) {
      LOG.info(
          "Getting throttle params for universe {} failed with: {}", universeUUID, e.getMessage());
      throw new RuntimeException(e.getMessage());
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  private Map<String, YbcThrottleParametersResponse.ThrottleParamValue> getThrottleParamsMap(
      UUID universeUUID, Map<String, Integer> currentValues) {
    Universe u = Universe.getOrBadRequest(universeUUID);
    NodeDetails n = u.getTServersInPrimaryCluster().get(0);
    String instanceType = n.cloudInfo.instance_type;
    int numCores =
        (int)
            Math.ceil(
                InstanceType.getOrBadRequest(
                        UUID.fromString(
                            u.getUniverseDetails()
                                .getClusterByUuid(n.placementUuid)
                                .userIntent
                                .provider),
                        instanceType)
                    .getNumCores());
    Map<String, YbcThrottleParametersResponse.ThrottleParamValue> throttleParams = new HashMap<>();
    throttleParams.put(
        GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS,
        new YbcThrottleParametersResponse.ThrottleParamValue(
            currentValues.get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS).intValue(),
            getPresetThrottleValues(numCores, ThrottleParamType.CONCURRENCY_PARAM)));
    throttleParams.put(
        GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS,
        new YbcThrottleParametersResponse.ThrottleParamValue(
            currentValues.get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS).intValue(),
            getPresetThrottleValues(numCores, ThrottleParamType.CONCURRENCY_PARAM)));
    throttleParams.put(
        GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS,
        new YbcThrottleParametersResponse.ThrottleParamValue(
            currentValues.get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS).intValue(),
            getPresetThrottleValues(numCores, ThrottleParamType.NUM_OBJECTS_PARAM)));
    throttleParams.put(
        GFlagsUtil.YBC_PER_UPLOAD_OBJECTS,
        new YbcThrottleParametersResponse.ThrottleParamValue(
            currentValues.get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS).intValue(),
            getPresetThrottleValues(numCores, ThrottleParamType.NUM_OBJECTS_PARAM)));
    return throttleParams;
  }

  private Map<FieldDescriptor, Object> getThrottleParamsAsFieldDescriptor(YbcClient ybcClient) {
    try {
      BackupServiceTaskThrottleParametersGetRequest throttleParametersGetRequest =
          BackupServiceTaskThrottleParametersGetRequest.getDefaultInstance();
      BackupServiceTaskThrottleParametersGetResponse throttleParametersGetResponse =
          ybcClient.backupServiceTaskThrottleParametersGet(throttleParametersGetRequest);
      if (throttleParametersGetResponse == null) {
        throw new RuntimeException("Get throttle parameters: No response from YB-Controller");
      }
      if (!throttleParametersGetResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
        throw new RuntimeException(
            String.format(
                "Getting throttle params failed with exception: %s",
                throttleParametersGetResponse.getStatus()));
      }
      ControllerObjectTaskThrottleParameters throttleParams =
          throttleParametersGetResponse.getParams();
      return throttleParams.getAllFields();
    } catch (Exception e) {
      LOG.info("Fetching throttle params failed");
      throw new RuntimeException(e.getMessage());
    }
  }

  public boolean deleteNfsDirectory(Backup backup) {
    YbcClient ybcClient = null;
    try {
      ybcClient = getYbcClient(backup.getUniverseUUID());
      CustomerConfigData configData =
          customerConfigService
              .getOrBadRequest(backup.getCustomerUUID(), backup.getBackupInfo().storageConfigUUID)
              .getDataObject();
      List<BackupServiceNfsDirDeleteRequest> nfsDirDeleteRequests =
          ((NFSUtil) storageUtilFactory.getStorageUtil("NFS"))
              .getBackupServiceNfsDirDeleteRequest(backup.getBackupParamsCollection(), configData);
      for (BackupServiceNfsDirDeleteRequest nfsDirDeleteRequest : nfsDirDeleteRequests) {
        BackupServiceNfsDirDeleteResponse nfsDirDeleteResponse =
            ybcClient.backupServiceNfsDirDelete(nfsDirDeleteRequest);
        if (!nfsDirDeleteResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
          LOG.error(
              "Nfs Dir deletion for backup {} failed with error: {}.",
              backup.getBackupUUID(),
              nfsDirDeleteResponse.getStatus().getErrorMessage());
          return false;
        }
      }
    } catch (Exception e) {
      LOG.error("Backup {} deletion failed with error: {}", backup.getBackupUUID(), e.getMessage());
      return false;
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
    LOG.debug("Nfs dir for backup {} is successfully deleted.", backup.getBackupUUID());
    return true;
  }

  public void abortBackupTask(
      UUID customerUUID, UUID backupUUID, String taskID, YbcClient ybcClient) {
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    try {
      BackupServiceTaskAbortRequest abortTaskRequest =
          BackupServiceTaskAbortRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskAbortResponse abortTaskResponse =
          ybcClient.backupServiceTaskAbort(abortTaskRequest);
      if (!abortTaskResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
        LOG.error(
            "Aborting backup {} task errored out with {}.",
            backup.getBackupUUID(),
            abortTaskResponse.getStatus().getErrorMessage());
        return;
      }
      BackupServiceTaskResultRequest taskResultRequest =
          BackupServiceTaskResultRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskResultResponse taskResultResponse =
          ybcClient.backupServiceTaskResult(taskResultRequest);
      if (!taskResultResponse.getTaskStatus().equals(ControllerStatus.ABORT)) {
        LOG.error(
            "Aborting backup {} task errored out and is in {} state.",
            backup.getBackupUUID(),
            taskResultResponse.getTaskStatus());
        return;
      } else {
        LOG.info(
            "Backup {} task is successfully aborted on Yb-controller.", backup.getBackupUUID());
        deleteYbcBackupTask(backup.getUniverseUUID(), taskID, ybcClient);
      }
    } catch (Exception e) {
      LOG.error(
          "Backup {} task abort failed with error: {}.", backup.getBackupUUID(), e.getMessage());
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  public void deleteYbcBackupTask(UUID universeUUID, String taskID, YbcClient ybcClient) {
    try {
      BackupServiceTaskResultRequest taskResultRequest =
          BackupServiceTaskResultRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskResultResponse taskResultResponse =
          ybcClient.backupServiceTaskResult(taskResultRequest);
      if (taskResultResponse.getTaskStatus().equals(ControllerStatus.NOT_FOUND)) {
        return;
      }
      BackupServiceTaskDeleteRequest taskDeleteRequest =
          BackupServiceTaskDeleteRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskDeleteResponse taskDeleteResponse = null;
      int numRetries = 0;
      while (numRetries < MAX_RETRIES) {
        taskDeleteResponse = ybcClient.backupServiceTaskDelete(taskDeleteRequest);
        if (!taskDeleteResponse.getStatus().getCode().equals(ControllerStatus.IN_PROGRESS)) {
          break;
        }
        Thread.sleep(WAIT_EACH_ATTEMPT_MS);
        numRetries++;
      }
      if (!taskDeleteResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
        LOG.error(
            "Deleting task {} errored out and is in {} state.",
            taskID,
            taskDeleteResponse.getStatus());
        return;
      }
      LOG.info("Task {} is successfully deleted on Yb-controller.", taskID);
    } catch (Exception e) {
      LOG.error("Task {} deletion failed with error: {}", taskID, e.getMessage());
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  /** Returns the success marker for a particular backup, returns null if not found. */
  public String downloadSuccessMarker(
      BackupServiceTaskCreateRequest downloadSuccessMarkerRequest,
      UUID universeUUID,
      String taskID) {
    YbcClient ybcClient = null;
    String successMarker = null;
    try {
      ybcClient = getYbcClient(universeUUID);
      BackupServiceTaskCreateResponse downloadSuccessMarkerResponse =
          ybcClient.restoreNamespace(downloadSuccessMarkerRequest);
      if (!downloadSuccessMarkerResponse.getStatus().getCode().equals(ControllerStatus.OK)) {
        throw new Exception(
            String.format(
                "Failed to send download success marker request, failure status: %s",
                downloadSuccessMarkerResponse.getStatus().getCode().name()));
      }
      BackupServiceTaskResultRequest downloadSuccessMarkerResultRequest =
          BackupServiceTaskResultRequest.newBuilder().setTaskId(taskID).build();
      BackupServiceTaskResultResponse downloadSuccessMarkerResultResponse = null;
      int numRetries = 0;
      while (numRetries < MAX_RETRIES) {
        downloadSuccessMarkerResultResponse =
            ybcClient.backupServiceTaskResult(downloadSuccessMarkerResultRequest);
        if (!(downloadSuccessMarkerResultResponse
                .getTaskStatus()
                .equals(ControllerStatus.IN_PROGRESS)
            || downloadSuccessMarkerResultResponse
                .getTaskStatus()
                .equals(ControllerStatus.NOT_STARTED))) {
          break;
        }
        Thread.sleep(WAIT_EACH_SHORT_ATTEMPT_MS);
        numRetries++;
      }
      if (!downloadSuccessMarkerResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
        throw new RuntimeException(
            String.format(
                "Failed to download success marker, failure status: %s",
                downloadSuccessMarkerResultResponse.getTaskStatus().name()));
      }
      LOG.info("Task {} on YB-Controller to fetch success marker is successful", taskID);
      successMarker = downloadSuccessMarkerResultResponse.getMetadataJson();
      deleteYbcBackupTask(universeUUID, taskID, ybcClient);
      return successMarker;
    } catch (Exception e) {
      LOG.error(
          "Task {} on YB-Controller to fetch success marker for restore failed. Error: {}",
          taskID,
          e.getMessage());
      return successMarker;
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  public BackupServiceTaskEnabledFeaturesResponse getEnabledBackupFeatures(UUID universeUUID) {
    YbcClient ybcClient = null;
    try {
      ybcClient = getYbcClient(universeUUID);
      return ybcClient.backupServiceTaskEnabledFeatures(
          BackupServiceTaskEnabledFeaturesRequest.getDefaultInstance());
    } catch (Exception e) {
      throw new RuntimeException(
          String.format(
              "Unable to fetch enabled backup features for Universe %s", universeUUID.toString()));
    } finally {
      ybcClientService.closeClient(ybcClient);
    }
  }

  public String getStableYbcVersion() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);
  }

  /**
   * @param universe
   * @param node
   * @return pair of string containing osType and archType of ybc-server-package
   */
  public Pair<String, String> getYbcPackageDetailsForNode(Universe universe, NodeDetails node) {
    Cluster nodeCluster = universe.getCluster(node.placementUuid);
    String ybSoftwareVersion = nodeCluster.userIntent.ybSoftwareVersion;
    String ybServerPackage =
        nodeManager.getYbServerPackageName(
            ybSoftwareVersion, getFirstRegion(universe, Objects.requireNonNull(nodeCluster)));
    return Util.getYbcPackageDetailsFromYbServerPackage(ybServerPackage);
  }

  /**
   * @param universe
   * @param node
   * @param ybcVersion
   * @return Temp location of ybc server package on a DB node.
   */
  public String getYbcPackageTmpLocation(Universe universe, NodeDetails node, String ybcVersion) {
    Pair<String, String> ybcPackageDetails = getYbcPackageDetailsForNode(universe, node);
    return String.format(
        "/tmp/ybc-%s-%s-%s.tar.gz",
        ybcVersion, ybcPackageDetails.getFirst(), ybcPackageDetails.getSecond());
  }

  /**
   * @param universe
   * @param node
   * @param ybcVersion
   * @return complete file path of ybc server package present in YBA node.
   */
  public String getYbcServerPackageForNode(Universe universe, NodeDetails node, String ybcVersion) {
    Pair<String, String> ybcPackageDetails = getYbcPackageDetailsForNode(universe, node);
    ReleaseManager.ReleaseMetadata releaseMetadata =
        releaseManager.getYbcReleaseByVersion(
            ybcVersion, ybcPackageDetails.getFirst(), ybcPackageDetails.getSecond());
    String ybcServerPackage =
        releaseMetadata.getFilePath(
            getFirstRegion(
                universe, Objects.requireNonNull(universe.getCluster(node.placementUuid))));
    if (StringUtils.isBlank(ybcServerPackage)) {
      throw new RuntimeException("Ybc package cannot be empty.");
    }
    Matcher matcher = NodeManager.YBC_PACKAGE_PATTERN.matcher(ybcServerPackage);
    boolean matches = matcher.matches();
    if (!matches) {
      throw new RuntimeException(
          String.format(
              "Ybc package: %s does not follow the format required: %s",
              ybcServerPackage, NodeManager.YBC_PACKAGE_REGEX));
    }
    return ybcServerPackage;
  }

  // Ping check for YbcClient.
  public boolean ybcPingCheck(String nodeIp, String certDir, int port) {
    YbcClient ybcClient = null;
    try {
      ybcClient = ybcClientService.getNewClient(nodeIp, port, certDir);
      return ybcPingCheck(ybcClient);
    } finally {
      try {
        ybcClientService.closeClient(ybcClient);
      } catch (Exception e) {
      }
    }
  }

  public boolean ybcPingCheck(YbcClient ybcClient) {
    if (ybcClient == null) {
      return false;
    }
    try {
      long pingSeq = new Random().nextInt();
      PingResponse pingResponse =
          ybcClient.ping(PingRequest.newBuilder().setSequence(pingSeq).build());
      if (pingResponse != null && pingResponse.getSequence() == pingSeq) {
        return true;
      }
      return false;
    } catch (Exception e) {
      return false;
    }
  }

  /**
   * Return YBC client and corresponding node-ip pair if available. If enablePreference is set, the
   * order of trying nodes with YBC ping check is pre-defined. The highest preference is given to
   * master leader, followed by same AZ nodes, followed by same region nodes.
   *
   * @param universeUUID The universe UUID
   * @param enablePreference If trying nodes for YBC ping check should be based on preference
   * @return The YBC client - node_ip pair
   */
  public Pair<YbcClient, String> getAvailableYbcClientIpPair(
      UUID universeUUID, boolean enablePreference) {
    if (enablePreference) {
      Universe universe = Universe.getOrBadRequest(universeUUID);
      List<String> nodeIPsWithPreference = getPreferenceBasedYBCNodeIPsList(universe);
      String certFile = universe.getCertificateNodetoNode();
      int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
      return getAvailableYbcClientIpPair(nodeIPsWithPreference, ybcPort, certFile);
    } else {
      return getAvailableYbcClientIpPair(universeUUID, null);
    }
  }

  /**
   * Returns a YbcClient <-> node-ip pair if available, after doing a ping check. If
   * nodeIPListOverride provided in params is non-null, will attempt to create client with only that
   * node-ip. Throws RuntimeException if client creation fails.
   *
   * @param universeUUID
   * @param nodeIPListOverride
   */
  public Pair<YbcClient, String> getAvailableYbcClientIpPair(
      UUID universeUUID, @Nullable List<String> nodeIPListOverride) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String certFile = universe.getCertificateNodetoNode();
    int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    List<String> nodeIPs = new ArrayList<>();
    if (CollectionUtils.isNotEmpty(nodeIPListOverride)) {
      nodeIPs = nodeIPListOverride;
    } else {
      nodeIPs.addAll(
          universe.getLiveTServersInPrimaryCluster().parallelStream()
              .map(nD -> nD.cloudInfo.private_ip)
              .collect(Collectors.toList()));
    }
    return getAvailableYbcClientIpPair(nodeIPs, ybcPort, certFile);
  }

  public Pair<YbcClient, String> getAvailableYbcClientIpPair(
      List<String> nodeIps, int ybcPort, String certFile) {
    Optional<Pair<YbcClient, String>> clientIpPair =
        nodeIps.stream()
            .map(
                ip -> {
                  YbcClient ybcClient = ybcClientService.getNewClient(ip, ybcPort, certFile);
                  return ybcPingCheck(ybcClient)
                      ? new Pair<YbcClient, String>(ybcClient, ip)
                      : null;
                })
            .filter(Objects::nonNull)
            .findFirst();
    if (!clientIpPair.isPresent()) {
      throw new RuntimeException("YB-Controller server unavailable");
    }
    return clientIpPair.get();
  }

  public static List<String> getPreferenceBasedYBCNodeIPsList(Universe universe) {
    return getPreferenceBasedYBCNodeIPsList(universe, new HashSet<>());
  }

  public static List<String> getPreferenceBasedYBCNodeIPsList(
      Universe universe, Set<String> excludeNodeIPs) {
    NodeDetails leaderMasterNodeDetails = universe.getMasterLeaderNode();
    List<String> nodesToCheckInPreference = new ArrayList<>();
    String masterLeaderIP = leaderMasterNodeDetails.cloudInfo.private_ip;
    // Add master leader to first preference.
    if (leaderMasterNodeDetails.isTserver && !excludeNodeIPs.contains(masterLeaderIP)) {
      nodesToCheckInPreference.add(masterLeaderIP);
    }

    // Give second preference to same AZ nodes.
    Set<String> sameAZNodes =
        universe.getLiveTServersInPrimaryCluster().stream()
            .filter(
                nD ->
                    !nD.cloudInfo.private_ip.equals(masterLeaderIP)
                        && !excludeNodeIPs.contains(masterLeaderIP)
                        && nD.getAzUuid().equals(leaderMasterNodeDetails.getAzUuid()))
            .map(nD -> nD.cloudInfo.private_ip)
            .collect(Collectors.toSet());
    nodesToCheckInPreference.addAll(sameAZNodes);

    // Give third preference to same region nodes.
    List<String> regionSortedList =
        universe.getLiveTServersInPrimaryCluster().stream()
            .filter(
                nD ->
                    !nD.cloudInfo.private_ip.equals(masterLeaderIP)
                        && !excludeNodeIPs.contains(masterLeaderIP)
                        && !sameAZNodes.contains(nD.cloudInfo.private_ip))
            .sorted(
                Comparator.<NodeDetails, Boolean>comparing(
                        nD -> nD.getRegion().equals(leaderMasterNodeDetails.getRegion()))
                    .reversed())
            .map(nD -> nD.cloudInfo.private_ip)
            .collect(Collectors.toList());
    nodesToCheckInPreference.addAll(regionSortedList);
    return nodesToCheckInPreference;
  }

  public YbcClient getYbcClient(List<String> nodeIps, int ybcPort, String certFile) {
    return getAvailableYbcClientIpPair(nodeIps, ybcPort, certFile).getFirst();
  }

  public YbcClient getYbcClient(UUID universeUUID) {
    return getYbcClient(universeUUID, null);
  }

  public YbcClient getYbcClient(UUID universeUUID, String nodeIp) {
    return getAvailableYbcClientIpPair(
            universeUUID, StringUtils.isBlank(nodeIp) ? null : Collections.singletonList(nodeIp))
        .getFirst();
  }

  private Region getFirstRegion(Universe universe, Cluster cluster) {
    Customer customer = Customer.get(universe.getCustomerId());
    UUID providerUuid = UUID.fromString(cluster.userIntent.provider);
    UUID regionUuid = cluster.userIntent.regionList.get(0);
    return Region.getOrBadRequest(customer.getUuid(), providerUuid, regionUuid);
  }

  public void copyYbcPackagesOnK8s(
      Map<String, String> config,
      Universe universe,
      NodeDetails nodeDetails,
      String ybcSoftwareVersion) {
    ReleaseManager.ReleaseMetadata releaseMetadata =
        releaseManager.getYbcReleaseByVersion(
            ybcSoftwareVersion,
            OsType.LINUX.toString().toLowerCase(),
            Architecture.x86_64.name().toLowerCase());
    String ybcPackage = releaseMetadata.filePath;
    UUID providerUUID =
        UUID.fromString(universe.getCluster(nodeDetails.placementUuid).userIntent.provider);
    Provider provider =
        Provider.get(Customer.get(universe.getCustomerId()).getUuid(), providerUUID);
    boolean listenOnAllInterfaces =
        confGetter.getConfForScope(provider, ProviderConfKeys.ybcListenOnAllInterfacesK8s);
    Map<String, String> ybcGflags =
        GFlagsUtil.getYbcFlagsForK8s(
            universe.getUniverseUUID(), nodeDetails.nodeName, listenOnAllInterfaces);
    try {
      Path confFilePath =
          Files.createTempFile(
              universe.getUniverseUUID().toString() + "_" + nodeDetails.nodeName, ".conf");
      Files.write(
          confFilePath,
          () ->
              ybcGflags.entrySet().stream()
                  .<CharSequence>map(e -> "--" + e.getKey() + "=" + e.getValue())
                  .iterator());
      kubernetesManagerFactory
          .getManager()
          .copyFileToPod(
              config,
              nodeDetails.cloudInfo.kubernetesNamespace,
              nodeDetails.cloudInfo.kubernetesPodName,
              "yb-controller",
              confFilePath.toAbsolutePath().toString(),
              "/mnt/disk0/yw-data/controller/conf/server.conf");
      kubernetesManagerFactory
          .getManager()
          .copyFileToPod(
              config,
              nodeDetails.cloudInfo.kubernetesNamespace,
              nodeDetails.cloudInfo.kubernetesPodName,
              "yb-controller",
              ybcPackage,
              "/mnt/disk0/yw-data/controller/tmp/");
    } catch (Exception ex) {
      LOG.error(ex.getMessage(), ex);
      throw new RuntimeException("Could not upload the ybc contents", ex);
    }
  }

  public void performActionOnYbcK8sNode(
      Map<String, String> config, NodeDetails nodeDetails, List<String> commandArgs) {
    kubernetesManagerFactory
        .getManager()
        .performYbcAction(
            config,
            nodeDetails.cloudInfo.kubernetesNamespace,
            nodeDetails.cloudInfo.kubernetesPodName,
            "yb-controller",
            commandArgs);
  }

  public void waitForYbc(Universe universe, Set<NodeDetails> nodeDetailsSet) {
    String certFile = universe.getCertificateNodetoNode();
    int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    String errMsg = "";

    LOG.info("Universe uuid: {}, ybcPort: {} to be used", universe.getUniverseUUID(), ybcPort);
    YbcClient client = null;
    boolean isYbcConfigured = true;
    Random rand = new Random();
    for (NodeDetails node : nodeDetailsSet) {
      String nodeIp = node.cloudInfo.private_ip;
      LOG.info("Node IP: {} to connect to YBC", nodeIp);

      try {
        client = ybcClientService.getNewClient(nodeIp, ybcPort, certFile);
        if (client == null) {
          throw new Exception("Could not create Ybc client.");
        }
        LOG.info("Node IP: {} Client created", nodeIp);
        long seqNum = rand.nextInt();
        PingRequest pingReq = PingRequest.newBuilder().setSequence(seqNum).build();
        int numTries = 0;
        do {
          LOG.info("Node IP: {} Making a ping request", nodeIp);
          PingResponse pingResp = client.ping(pingReq);
          if (pingResp != null && pingResp.getSequence() == seqNum) {
            LOG.info("Node IP: {} Ping successful", nodeIp);
            break;
          } else if (pingResp == null) {
            numTries++;
            long waitTimeInMillis =
                INITIAL_SLEEP_TIME_IN_MS + INCREMENTAL_SLEEP_TIME_IN_MS * (numTries - 1);
            LOG.info(
                "Node IP: {} Ping not complete. Sleeping for {} millis", nodeIp, waitTimeInMillis);
            Duration duration = Duration.ofMillis(waitTimeInMillis);
            Thread.sleep(duration.toMillis());
            if (numTries <= MAX_NUM_RETRIES) {
              LOG.info("Node IP: {} Ping not complete. Continuing", nodeIp);
              continue;
            }
          }
          if (numTries > MAX_NUM_RETRIES) {
            LOG.info("Node IP: {} Ping failed. Exceeded max retries", nodeIp);
            errMsg = String.format("Exceeded max retries: %s", MAX_NUM_RETRIES);
            isYbcConfigured = false;
            break;
          } else if (pingResp.getSequence() != seqNum) {
            errMsg =
                String.format(
                    "Returned incorrect seqNum. Expected: %s, Actual: %s",
                    seqNum, pingResp.getSequence());
            isYbcConfigured = false;
            break;
          }
        } while (true);
      } catch (Exception e) {
        LOG.error("{} hit error : {}", "WaitForYbcServer", e.getMessage());
        throw new RuntimeException(e);
      } finally {
        ybcClientService.closeClient(client);
        if (!isYbcConfigured) {
          throw new RuntimeException(
              String.format("Exception in pinging yb-controller server at %s: %s", nodeIp, errMsg));
        }
      }
    }
  }
}
