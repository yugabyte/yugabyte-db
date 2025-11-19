package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.common.operator.helpers.KubernetesOverridesSerializer;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse.ThrottleParamValue;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.ObjectMetaBuilder;
import io.fabric8.kubernetes.api.model.OwnerReference;
import io.fabric8.kubernetes.api.model.OwnerReferenceBuilder;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.ConfigBuilder;
import io.fabric8.kubernetes.client.CustomResource;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.BackupSchedule;
import io.yugabyte.operator.v1alpha1.BackupSpec;
import io.yugabyte.operator.v1alpha1.BackupStatus;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.YBProvider;
import io.yugabyte.operator.v1alpha1.YBProviderSpec;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.releasespec.config.DownloadConfig;
import io.yugabyte.operator.v1alpha1.ybproviderspec.Regions;
import io.yugabyte.operator.v1alpha1.ybproviderspec.regions.Zones;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YbcThrottleParameters;
import java.nio.file.Paths;
import java.util.Base64;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

@Slf4j
public class OperatorUtils {

  public static final String IGNORE_RECONCILER_ADD_LABEL = "ignore-reconciler-add";
  public static final String YB_FINALIZER = "finalizer.k8soperator.yugabyte.com";
  public static final String AUTO_PROVIDER_LABEL = "auto-provider";

  private static final String[] ZONE_CONFIG_KEYS_TO_CHECK = {
    "KUBENAMESPACE",
    "OVERRIDES",
    "KUBE_POD_ADDRESS_TEMPLATE",
    "KUBE_DOMAIN",
    "CERT-MANAGER-ISSUER-KIND",
    "CERT-MANAGER-ISSUER-NAME",
    "CERT-MANAGER-ISSUER-GROUP",
    "STORAGE_CLASS"
  };

  private final RuntimeConfGetter confGetter;
  private final String namespace;
  private final YbcManager ybcManager;
  private final ValidatingFormFactory validatingFormFactory;

  private Config _k8sClientConfig;
  private ReleaseManager releaseManager;
  private ObjectMapper objectMapper;

  @Inject
  public OperatorUtils(
      RuntimeConfGetter confGetter,
      ReleaseManager releaseManager,
      YbcManager ybcManager,
      ValidatingFormFactory validatingFormFactory) {
    this.releaseManager = releaseManager;
    this.confGetter = confGetter;
    this.ybcManager = ybcManager;
    namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    this.validatingFormFactory = validatingFormFactory;
    this.objectMapper = new ObjectMapper();
  }

  private KubernetesClient createKubernetesClient() {
    return new KubernetesClientBuilder().withConfig(getK8sClientConfig()).build();
  }

  private synchronized Config getK8sClientConfig() {
    if (_k8sClientConfig == null) {
      ConfigBuilder confBuilder = new ConfigBuilder();
      if (namespace == null || namespace.trim().isEmpty()) {
        confBuilder.withNamespace(null);
      } else {
        confBuilder.withNamespace(namespace);
      }
      _k8sClientConfig = confBuilder.build();
    }
    return _k8sClientConfig;
  }

  public Customer getOperatorCustomer() throws Exception {
    // If the customer UUID is set in the config, use that.
    if (!StringUtils.isEmpty(
        confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorCustomerUUID))) {
      UUID operatorCustomerUUID =
          UUID.fromString(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorCustomerUUID));
      return Customer.get(operatorCustomerUUID);
    }
    // Otherwise, if there is only one customer, use that. If more than one customer is found
    // Raise Exception.
    List<Customer> custList = Customer.getAll();
    if (custList.size() != 1) {
      throw new Exception("Customer list does not have exactly one customer.");
    }
    Customer cust = custList.get(0);
    return cust;
  }

  public String getCustomerUUID() throws Exception {
    Customer cust = getOperatorCustomer();
    return cust.getUuid().toString();
  }

  public Universe getUniverseFromNameAndNamespace(
      Long customerId, String universeName, String namespace) throws Exception {
    KubernetesResourceDetails ybUniverseResourceDetails = new KubernetesResourceDetails();
    ybUniverseResourceDetails.name = universeName;
    ybUniverseResourceDetails.namespace = namespace;
    YBUniverse ybUniverse = getYBUniverse(ybUniverseResourceDetails);
    Optional<Universe> universe =
        Universe.maybeGetUniverseByName(customerId, getYbaResourceName(ybUniverse.getMetadata()));
    if (universe.isPresent()) {
      return universe.get();
    }
    return null;
  }

  public YBUniverse getYBUniverse(KubernetesResourceDetails name) throws Exception {
    try (final KubernetesClient kubernetesClient = createKubernetesClient()) {
      log.debug("lookup ybuniverse {}/{}", name.namespace, name.name);
      return kubernetesClient
          .resources(YBUniverse.class)
          .inNamespace(name.namespace)
          .withName(name.name)
          .get();
    } catch (Exception e) {
      throw new Exception("Unable to fetch YBUniverse " + name.name, e);
    }
  }

  /**
   * Get owner reference generated from a specific resource Also contains the YBA UUID of the
   * resource in additional properties.
   *
   * @param <T>
   * @param resourceDetails The KubernetesResourceDetails of the resource
   * @param clazz The custom resource class
   * @return
   * @throws Exception
   */
  public <T extends CustomResource<?, ?>> OwnerReference getResourceOwnerReference(
      KubernetesResourceDetails resourceDetails, Class<T> clazz) throws Exception {
    try (final KubernetesClient kubernetesClient = createKubernetesClient()) {
      T resource = getResource(resourceDetails, kubernetesClient.resources(clazz), clazz);
      return new OwnerReferenceBuilder()
          .withKind(resource.getKind())
          .withName(resourceDetails.name)
          .withUid(resource.getMetadata().getUid())
          .withApiVersion(resource.getApiVersion())
          .withBlockOwnerDeletion(true)
          .build();
    } catch (Exception e) {
      throw new Exception(
          String.format(
              "Unable to fetch resource: %s type: %s", resourceDetails.name, clazz.getSimpleName()),
          e);
    }
  }

  /**
   * Get the custom resource
   *
   * @param <T>
   * @param resourceDetails The KubernetesResourceDetails of the resource
   * @param client The KubernetesClient
   * @return
   */
  public <T extends CustomResource<?, ?>> T getResource(
      KubernetesResourceDetails resourceDetails,
      MixedOperation<T, KubernetesResourceList<T>, Resource<T>> client,
      Class<T> clazz) {
    log.trace(
        "lookup resource {} {}/{}",
        clazz.getSimpleName(),
        resourceDetails.namespace,
        resourceDetails.name);
    return client.inNamespace(resourceDetails.namespace).withName(resourceDetails.name).get();
  }

  /**
   * Remove finalizer from resource. Only the finalizer added by Yugaware:
   * "finalizer.k8soperator.yugabyte.com" is removed.
   *
   * @param <T>
   * @param resource The custom resource
   * @param client The client
   */
  public <T extends CustomResource<?, ?>> void removeFinalizer(
      T resource, MixedOperation<T, KubernetesResourceList<T>, Resource<T>> client) {
    // Remove only operator added finalizer
    if (resource.getMetadata() != null) {
      log.info("Removing finalizer for resource:{}", resource.getMetadata().getName());
      ObjectMeta objectMeta = resource.getMetadata();
      List<String> finalizers = objectMeta.getFinalizers();
      if (CollectionUtils.isNotEmpty(finalizers)) {
        finalizers.remove(YB_FINALIZER);
        client
            .inNamespace(objectMeta.getNamespace())
            .withName(objectMeta.getName())
            .patch(resource);
      }
    }
  }

  public static String getYbaResourceName(ObjectMeta objectMeta) {
    String name = objectMeta.getName();
    String namespace = objectMeta.getNamespace();
    String uid = objectMeta.getUid();
    int hashCode = name.concat(namespace).concat(uid).hashCode();
    return name.concat("-").concat(Integer.toString(Math.abs(hashCode)));
  }

  /*--- YBUniverse related help methods ---*/

  public boolean shouldUpdateYbUniverse(
      UserIntent currentUserIntent,
      int newNumNodes,
      DeviceInfo newDeviceInfo,
      DeviceInfo newMasterDeviceInfo) {
    return !(currentUserIntent.numNodes == newNumNodes)
        || !currentUserIntent.deviceInfo.volumeSize.equals(newDeviceInfo.volumeSize)
        || !currentUserIntent.masterDeviceInfo.volumeSize.equals(newMasterDeviceInfo.volumeSize);
  }

  public String getKubernetesOverridesString(Object kubernetesOverrides) {
    if (kubernetesOverrides == null) {
      return null;
    }
    ObjectMapper mapper = new ObjectMapper(new YAMLFactory());
    mapper.setSerializationInclusion(Include.NON_NULL);
    mapper.setSerializationInclusion(Include.NON_EMPTY);
    SimpleModule simpleModule = new SimpleModule();
    simpleModule.addSerializer(new KubernetesOverridesSerializer());
    mapper.registerModule(simpleModule);
    try {
      return mapper.writeValueAsString(kubernetesOverrides);
    } catch (Exception e) {
      log.error("Unable to parse universe overrides", e);
    }
    return null;
  }

  public boolean checkIfGFlagsChanged(
      Universe universe, SpecificGFlags oldGFlags, SpecificGFlags newGFlags) {
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    return universe.getNodesByCluster(primaryCluster.uuid).stream()
        .filter(
            nD -> {
              // New gflags for servers
              Map<String, String> newTserverGFlags =
                  newGFlags.getGFlags(nD.getAzUuid(), ServerType.TSERVER);
              Map<String, String> newMasterGFlags =
                  newGFlags.getGFlags(nD.getAzUuid(), ServerType.MASTER);

              // Old gflags for servers
              Map<String, String> oldTserverGFlags =
                  oldGFlags.getGFlags(nD.getAzUuid(), ServerType.TSERVER);
              Map<String, String> oldMasterGFlags =
                  oldGFlags.getGFlags(nD.getAzUuid(), ServerType.MASTER);
              return !(oldTserverGFlags.equals(newTserverGFlags)
                  && oldMasterGFlags.equals(newMasterGFlags));
            })
        .findAny()
        .isPresent();
  }

  public SpecificGFlags getGFlagsFromSpec(YBUniverse ybUniverse, Provider provider) {
    SpecificGFlags specificGFlags = new SpecificGFlags();
    if (ybUniverse.getSpec().getGFlags() != null) {
      SpecificGFlags.PerProcessFlags perProcessFlags = new PerProcessFlags();
      if (ybUniverse.getSpec().getGFlags().getTserverGFlags() != null) {
        perProcessFlags.value.put(
            ServerType.TSERVER, ybUniverse.getSpec().getGFlags().getTserverGFlags());
      }
      if (ybUniverse.getSpec().getGFlags().getMasterGFlags() != null) {
        perProcessFlags.value.put(
            ServerType.MASTER, ybUniverse.getSpec().getGFlags().getMasterGFlags());
      }
      specificGFlags.setPerProcessFlags(perProcessFlags);
      if (ybUniverse.getSpec().getGFlags().getPerAZ() != null) {
        Map<UUID, SpecificGFlags.PerProcessFlags> azOverridesMap = new HashMap<>();
        ybUniverse.getSpec().getGFlags().getPerAZ().entrySet().stream()
            .forEach(
                e -> {
                  Optional<AvailabilityZone> oAz =
                      AvailabilityZone.maybeGetByCode(provider, e.getKey());
                  if (oAz.isPresent()) {
                    SpecificGFlags.PerProcessFlags pPFlags = new PerProcessFlags();
                    if (e.getValue().getTserverGFlags() != null) {
                      pPFlags.value.put(ServerType.TSERVER, e.getValue().getTserverGFlags());
                    }
                    if (e.getValue().getMasterGFlags() != null) {
                      pPFlags.value.put(ServerType.MASTER, e.getValue().getMasterGFlags());
                    }
                    azOverridesMap.put(oAz.get().getUuid(), pPFlags);
                  }
                });
        specificGFlags.setPerAZ(azOverridesMap);
      }
    }
    return specificGFlags;
  }

  public boolean checkIfPlacementInfoChanged(
      PlacementInfo oldPlacementInfo, YBUniverse ybUniverse) {
    // If placement info is not set in the CR, return false
    if (ybUniverse.getSpec().getPlacementInfo() == null) {
      return false;
    }
    JsonNode oldPlacementCloud = objectMapper.valueToTree(oldPlacementInfo.cloudList.get(0));
    JsonNode newPlacementCloud = objectMapper.valueToTree(ybUniverse.getSpec().getPlacementInfo());

    Map<String, JsonNode> oldRegions = mapByCode(oldPlacementCloud.path("regionList"));
    Map<String, JsonNode> newRegions = mapByCode(newPlacementCloud.path("regions"));

    // Detect region changes
    if (!oldRegions.keySet().equals(newRegions.keySet())) {
      log.info("region mismatch for universe: {}", ybUniverse.getMetadata().getName());
      return true;
    }

    // For each region, compare zones
    for (String region : oldRegions.keySet()) {
      // Need to map by name for zones
      Map<String, JsonNode> oldZones = mapByName(oldRegions.get(region).path("azList"));
      Map<String, JsonNode> newZones = mapByCode(newRegions.get(region).path("zones"));

      if (!oldZones.keySet().equals(newZones.keySet())) {
        log.info("az list mismatch for universe: {}", ybUniverse.getMetadata().getName());
        return true;
      }

      // Compare zone fields
      for (String zone : oldZones.keySet()) {
        JsonNode oldZone = oldZones.get(zone);
        JsonNode newZone = newZones.get(zone);

        if (oldZone.path("numNodesInAZ").asInt(-1) != newZone.path("numNodes").asInt(-1)) {
          return true;
        }
        if (oldZone.path("isAffinitized").asBoolean(false)
            != newZone.path("preferred").asBoolean(false)) {
          return true;
        }
      }
    }
    return false; // no changes
  }

  private static Map<String, JsonNode> mapByCode(JsonNode array) {
    return mapByKey(array, "code");
  }

  private static Map<String, JsonNode> mapByName(JsonNode array) {
    return mapByKey(array, "name");
  }

  private static Map<String, JsonNode> mapByKey(JsonNode array, String key) {
    Map<String, JsonNode> map = new HashMap<>();
    if (array != null && array.isArray()) {
      for (JsonNode elem : array) {
        String code = elem.path(key).asText(null);
        if (code != null) {
          map.put(code, elem);
        }
      }
    }
    return map;
  }

  public DeviceInfo mapDeviceInfo(io.yugabyte.operator.v1alpha1.ybuniversespec.DeviceInfo spec) {
    DeviceInfo di = new DeviceInfo();

    Long numVols = spec.getNumVolumes();
    if (numVols != null) {
      di.numVolumes = numVols.intValue();
    }

    Long volSize = spec.getVolumeSize();
    if (volSize != null) {
      di.volumeSize = volSize.intValue();
    }

    di.storageClass = spec.getStorageClass();

    return di;
  }

  public DeviceInfo mapMasterDeviceInfo(
      io.yugabyte.operator.v1alpha1.ybuniversespec.MasterDeviceInfo spec) {
    DeviceInfo di = new DeviceInfo();

    if (spec == null) {
      return defaultMasterDeviceInfo();
    }

    Long numVols = spec.getNumVolumes();
    if (numVols != null) {
      di.numVolumes = numVols.intValue();
    }

    Long volSize = spec.getVolumeSize();
    if (volSize != null) {
      di.volumeSize = volSize.intValue();
    }

    di.storageClass = spec.getStorageClass();

    return di;
  }

  public DeviceInfo defaultMasterDeviceInfo() {
    DeviceInfo masterDeviceInfo = new DeviceInfo();
    masterDeviceInfo.volumeSize = 50;
    masterDeviceInfo.numVolumes = 1;
    return masterDeviceInfo;
  }

  public boolean universeAndSpecMismatch(Customer cust, Universe u, YBUniverse ybUniverse) {
    return universeAndSpecMismatch(cust, u, ybUniverse, null);
  }

  public boolean universeAndSpecMismatch(
      Customer cust, Universe u, YBUniverse ybUniverse, @Nullable TaskInfo prevTaskToRerun) {
    UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
    if (universeDetails == null || universeDetails.getPrimaryCluster() == null) {
      throw new RuntimeException(
          String.format("Invalid universe details found for {}", u.getName()));
    }

    UserIntent currentUserIntent = universeDetails.getPrimaryCluster().userIntent;

    // Handle previously unset masterDeviceInfo
    if (currentUserIntent.masterDeviceInfo == null) {
      currentUserIntent.masterDeviceInfo = defaultMasterDeviceInfo();
    }

    Provider provider =
        Provider.getOrBadRequest(cust.getUuid(), UUID.fromString(currentUserIntent.provider));
    // Get all required params
    SpecificGFlags specGFlags = getGFlagsFromSpec(ybUniverse, provider);
    String incomingOverrides =
        getKubernetesOverridesString(ybUniverse.getSpec().getKubernetesOverrides());
    String incomingYbSoftwareVersion = ybUniverse.getSpec().getYbSoftwareVersion();
    DeviceInfo incomingDeviceInfo = mapDeviceInfo(ybUniverse.getSpec().getDeviceInfo());
    DeviceInfo incomingMasterDeviceInfo =
        mapMasterDeviceInfo(ybUniverse.getSpec().getMasterDeviceInfo());
    int incomingNumNodes = (int) ybUniverse.getSpec().getNumNodes().longValue();
    Boolean pauseChangeRequired =
        ybUniverse.getSpec().getPaused() != u.getUniverseDetails().universePaused;

    if (prevTaskToRerun != null) {
      TaskType specificTaskTypeToRerun = prevTaskToRerun.getTaskType();
      switch (specificTaskTypeToRerun) {
        case EditKubernetesUniverse:
          UniverseDefinitionTaskParams prevTaskParams =
              Json.fromJson(prevTaskToRerun.getTaskParams(), UniverseDefinitionTaskParams.class);
          return shouldUpdateYbUniverse(
              prevTaskParams.getPrimaryCluster().userIntent,
              incomingNumNodes,
              incomingDeviceInfo,
              incomingMasterDeviceInfo);
        case KubernetesOverridesUpgrade:
          KubernetesOverridesUpgradeParams overridesUpgradeTaskParams =
              Json.fromJson(
                  prevTaskToRerun.getTaskParams(), KubernetesOverridesUpgradeParams.class);
          return !StringUtils.equals(
              incomingOverrides, overridesUpgradeTaskParams.universeOverrides);
        case GFlagsKubernetesUpgrade:
          KubernetesGFlagsUpgradeParams gflagParams =
              Json.fromJson(prevTaskToRerun.getTaskParams(), KubernetesGFlagsUpgradeParams.class);
          return checkIfGFlagsChanged(
              u, gflagParams.getPrimaryCluster().userIntent.specificGFlags, specGFlags);
        default:
          // Return false for re-run cases.
          return false;
      }
    }
    Boolean mismatch = false;
    mismatch =
        mismatch || !StringUtils.equals(incomingOverrides, currentUserIntent.universeOverrides);
    log.trace("overrides mismatch: {}", mismatch);
    mismatch =
        mismatch
            || checkIfGFlagsChanged(
                u,
                u.getUniverseDetails()
                    .getPrimaryCluster()
                    .userIntent
                    .specificGFlags /*Current gflags */,
                specGFlags);
    log.trace("gflags mismatch: {}", mismatch);
    mismatch =
        mismatch
            || checkIfPlacementInfoChanged(
                u.getUniverseDetails().getPrimaryCluster().placementInfo, ybUniverse);
    log.trace("placement info mismatch: {}", mismatch);
    mismatch =
        mismatch
            || shouldUpdateYbUniverse(
                currentUserIntent, incomingNumNodes, incomingDeviceInfo, incomingMasterDeviceInfo);
    log.trace("nodes mismatch: {}", mismatch);
    mismatch =
        mismatch
            || !StringUtils.equals(currentUserIntent.ybSoftwareVersion, incomingYbSoftwareVersion);
    log.trace("version mismatch: {}", mismatch);
    mismatch = mismatch || pauseChangeRequired;
    log.trace("pause mismatch: {}", mismatch);
    mismatch = mismatch || isThrottleParamUpdate(u, ybUniverse);
    log.trace("throttle mismatch: {}", mismatch);
    return mismatch;
  }

  /*--- Release related help methods ---*/

  public static Pair<String, ReleaseMetadata> crToReleaseMetadata(Release release) {
    DownloadConfig downloadConfig = release.getSpec().getConfig().getDownloadConfig();
    String version = release.getSpec().getConfig().getVersion();
    ReleaseMetadata metadata = ReleaseMetadata.create(version);
    if (downloadConfig.getS3() != null) {
      metadata.s3 = new ReleaseMetadata.S3Location();
      metadata.s3.paths = new ReleaseMetadata.PackagePaths();
      metadata.s3.accessKeyId = downloadConfig.getS3().getAccessKeyId();
      metadata.s3.secretAccessKey = downloadConfig.getS3().getSecretAccessKey();
      metadata.s3.paths.x86_64 = downloadConfig.getS3().getPaths().getX86_64();
      metadata.filePath = downloadConfig.getS3().getPaths().getX86_64();
      metadata.s3.paths.x86_64_checksum = downloadConfig.getS3().getPaths().getX86_64_checksum();
      metadata.s3.paths.helmChart = downloadConfig.getS3().getPaths().getHelmChart();
      metadata.s3.paths.helmChartChecksum =
          downloadConfig.getS3().getPaths().getHelmChartChecksum();
    }

    if (downloadConfig.getGcs() != null) {
      metadata.gcs = new ReleaseMetadata.GCSLocation();
      metadata.gcs.paths = new ReleaseMetadata.PackagePaths();
      metadata.gcs.credentialsJson = downloadConfig.getGcs().getCredentialsJson();
      metadata.gcs.paths.x86_64 = downloadConfig.getGcs().getPaths().getX86_64();
      metadata.filePath = downloadConfig.getGcs().getPaths().getX86_64();
      metadata.gcs.paths.x86_64_checksum = downloadConfig.getGcs().getPaths().getX86_64_checksum();
      metadata.gcs.paths.helmChart = downloadConfig.getGcs().getPaths().getHelmChart();
      metadata.gcs.paths.helmChartChecksum =
          downloadConfig.getGcs().getPaths().getHelmChartChecksum();
    }

    if (downloadConfig.getHttp() != null) {
      metadata.http = new ReleaseMetadata.HttpLocation();
      metadata.http.paths = new ReleaseMetadata.PackagePaths();
      metadata.http.paths.x86_64 = downloadConfig.getHttp().getPaths().getX86_64();
      metadata.filePath = downloadConfig.getHttp().getPaths().getX86_64();
      metadata.http.paths.x86_64_checksum =
          downloadConfig.getHttp().getPaths().getX86_64_checksum();
      metadata.http.paths.helmChart = downloadConfig.getHttp().getPaths().getHelmChart();
      metadata.http.paths.helmChartChecksum =
          downloadConfig.getHttp().getPaths().getHelmChartChecksum();
    }
    Pair<String, ReleaseMetadata> output = new Pair<>(version, metadata);
    return output;
  }

  public void deleteReleaseCr(Release release) {
    ObjectMeta releaseMetadata = release.getMetadata();
    log.info("Removing Release {}", releaseMetadata.getName());
    Pair<String, ReleaseMetadata> releasePair = crToReleaseMetadata(release);
    try (final KubernetesClient kubernetesClient = createKubernetesClient()) {
      if (releaseManager.getInUse(releasePair.getFirst())) {
        log.info("Release " + releasePair.getFirst() + " is in use!, Skipping deletion");
        return;
      }
      releaseManager.removeRelease(releasePair.getFirst());
      releaseManager.updateCurrentReleases();
      log.info("Removing finalizers from release {}", releaseMetadata.getName());
      releaseMetadata.setFinalizers(Collections.emptyList());
      kubernetesClient
          .resources(Release.class)
          .inNamespace(releaseMetadata.getNamespace())
          .withName(releaseMetadata.getName())
          .patch(release);
    } catch (RuntimeException re) {
      log.error("Error in deleting release", re);
    }
    log.info("Removed release {}", release.getMetadata().getName());
  }

  public String getAndParseSecretForKey(String name, @Nullable String namespace, String key) {
    Secret secret = getSecret(name, namespace);
    if (secret == null) {
      log.warn("Secret {} not found", name);
      return null;
    }
    return parseSecretForKey(secret, key);
  }

  public Secret getSecret(String name, @Nullable String namespace) {
    try (final KubernetesClient kubernetesClient = createKubernetesClient()) {
      if (StringUtils.isBlank(namespace)) {
        log.info("Getting secret '{}' from default namespace", name);
        namespace = "default";
      }
      return kubernetesClient.secrets().inNamespace(namespace).withName(name).get();
    }
  }

  // parseSecretForKey checks secret data for the key. If not found, it will then check stringData.
  // Returns null if the key is not found at all.
  // Also handles null secret.
  public String parseSecretForKey(Secret secret, String key) {
    if (secret == null) {
      return null;
    }
    if (secret.getData().get(key) != null) {
      return new String(Base64.getDecoder().decode(secret.getData().get(key)));
    }
    return secret.getStringData().get(key);
  }

  /*
   * Determines if there is a need to update throttle parameters for a given universe.
   *
   * This method compares the current throttle parameters of the universe with the specified
   * parameters in the YBUniverse specification. If the specification parameters are not defined,
   * it checks if the current parameters are set to their default values as obtained from the YBC
   * API. If there is any mismatch between the current and specified parameters, or if the current
   * parameters are not set to their default values when no specification is provided, the method
   * returns true, indicating an update is required.
   *
   * @param universe the Universe object representing the current state of the universe.
   * @param ybUniverse the YBUniverse object containing the specification for throttle parameters.
   * @return true if an update to throttle parameters is needed; false otherwise.
   * @throws RuntimeException if an unknown throttle parameter is encountered.
   */
  public boolean isThrottleParamUpdate(Universe universe, YBUniverse ybUniverse) {
    YbcThrottleParameters specParams = ybUniverse.getSpec().getYbcThrottleParameters();
    YbcThrottleParametersResponse currentParams =
        ybcManager.getThrottleParams(universe.getUniverseUUID());
    for (String key : currentParams.getThrottleParamsMap().keySet()) {
      ThrottleParamValue currentParam = currentParams.getThrottleParamsMap().get(key);
      // when spec params is not defined, we need to ensure all current throttle params are set to
      // their default values
      // according to the YBC Api to get them.
      if (specParams == null) {
        if (currentParam.getPresetValues().getDefaultValue() != currentParam.getCurrentValue())
          return true;
      } else {
        Long value = (long) currentParam.getCurrentValue();
        switch (key) {
          case GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS:
            if (value != specParams.getMaxConcurrentDownloads()) return true;
            break;
          case GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS:
            if (value != specParams.getMaxConcurrentUploads()) return true;
            break;
          case GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS:
            if (value != specParams.getPerDownloadNumObjects()) return true;
            break;
          case GFlagsUtil.YBC_PER_UPLOAD_OBJECTS:
            if (value != specParams.getPerUploadNumObjects()) return true;
            break;
          default:
            // This shoud only happen if a new throttle parameter is introduced and not added here.
            throw new RuntimeException("Unknown throttle parameter: " + key);
        }
      }
    }
    return false;
  }

  /*--- Backup and Scheduled backup helper methods ---*/

  public UUID getStorageConfigUUIDFromName(
      String scName, SharedIndexInformer<StorageConfig> scInformer) {
    Lister<StorageConfig> scLister = new Lister<>(scInformer.getIndexer());
    List<StorageConfig> storageConfigs = scLister.list();

    for (StorageConfig storageConfig : storageConfigs) {
      if (storageConfig.getMetadata().getName().equals(scName)) {
        return UUID.fromString(storageConfig.getStatus().getResourceUUID());
      }
    }
    return null;
  }

  public BackupRequestParams getScheduleBackupRequestFromCr(
      BackupSchedule backupSchedule, SharedIndexInformer<StorageConfig> scInformer)
      throws Exception {
    JsonNode crParams = objectMapper.valueToTree(backupSchedule.getSpec());
    BackupRequestParams backupRequestParams =
        getBackupRequestFromCr(crParams, backupSchedule.getMetadata().getNamespace(), scInformer);
    backupRequestParams.baseBackupUUID = null;
    backupRequestParams.scheduleName = getYbaResourceName(backupSchedule.getMetadata());
    backupRequestParams.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(backupSchedule));
    return backupRequestParams;
  }

  public BackupRequestParams getBackupRequestFromCr(
      Backup backup, SharedIndexInformer<StorageConfig> scInformer) throws Exception {
    JsonNode crParams = objectMapper.valueToTree(backup.getSpec());
    BackupRequestParams backupRequestParams =
        getBackupRequestFromCr(crParams, backup.getMetadata().getNamespace(), scInformer);
    backupRequestParams.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(backup));
    return backupRequestParams;
  }

  @VisibleForTesting
  BackupRequestParams getBackupRequestFromCr(
      JsonNode crParams, String namespace, SharedIndexInformer<StorageConfig> scInformer)
      throws Exception {
    Customer cust;
    try {
      cust = getOperatorCustomer();
    } catch (Exception e) {
      log.error("Got Exception in getting customer", e);
      return null;
    }

    String crUniverseName = ((ObjectNode) crParams).get("universe").asText();
    String crStorageConfig = ((ObjectNode) crParams).get("storageConfig").asText();
    Universe universe = getUniverseFromNameAndNamespace(cust.getId(), crUniverseName, namespace);
    if (universe == null) {
      throw new Exception("No universe found with name " + crUniverseName);
    }
    UUID universeUUID = universe.getUniverseUUID();
    UUID storageConfigUUID = getStorageConfigUUIDFromName(crStorageConfig, scInformer);

    if (storageConfigUUID == null) {
      throw new Exception("No storage config found with name " + crStorageConfig);
    }

    KeyspaceTable kT = new KeyspaceTable();
    if (((ObjectNode) crParams).has("keyspace")) {
      kT.keyspace = ((ObjectNode) crParams).get("keyspace").asText();
      ((ObjectNode) crParams).remove("keyspace");
    }
    ((ObjectNode) crParams).set("keyspaceTableList", Json.toJson(kT));

    ((ObjectNode) crParams).put("universeUUID", universeUUID.toString());
    ((ObjectNode) crParams).put("storageConfigUUID", storageConfigUUID.toString());
    ((ObjectNode) crParams).put("customerUUID", cust.getUuid().toString());
    ((ObjectNode) crParams).put("expiryTimeUnit", "MILLISECONDS");
    ((ObjectNode) crParams).put("frequencyTimeUnit", "MILLISECONDS");
    ((ObjectNode) crParams).put("incrementalBackupFrequencyTimeUnit", "MILLISECONDS");

    if (((ObjectNode) crParams).has("cronExpression")
        && StringUtils.isBlank(((ObjectNode) crParams).get("cronExpression").asText())) {
      ((ObjectNode) crParams).remove("cronExpression");
    }

    if (((ObjectNode) crParams).has("incrementalBackupBase")
        && StringUtils.isNotBlank(((ObjectNode) crParams).get("incrementalBackupBase").asText())) {
      String baseBackupName = ((ObjectNode) crParams).get("incrementalBackupBase").asText();
      com.yugabyte.yw.models.Backup baseBackup = getBaseBackup(baseBackupName, namespace, cust);
      if (!storageConfigUUID.equals(baseBackup.getStorageConfigUUID())
          || !universeUUID.equals(baseBackup.getUniverseUUID())) {
        throw new Exception(
            "Invalid cr values: Storage config and Universe should be same for incremental backup");
      }
      ((ObjectNode) crParams).put("baseBackupUUID", baseBackup.getBaseBackupUUID().toString());
    }

    return validatingFormFactory.getFormDataOrBadRequest(crParams, BackupRequestParams.class);
  }

  @VisibleForTesting
  com.yugabyte.yw.models.Backup getBaseBackup(
      String basebackupCrName, String namespace, Customer customer) throws Exception {
    Backup backup = null;
    try (final KubernetesClient kubernetesClient = createKubernetesClient()) {
      backup =
          getResource(
              new KubernetesResourceDetails(basebackupCrName, namespace),
              kubernetesClient.resources(Backup.class),
              Backup.class);
    }
    if (backup == null) {
      throw new Exception(String.format("Backup: %s cr not found", basebackupCrName));
    }
    if (backup.getStatus() == null || backup.getStatus().getResourceUUID() == null) {
      throw new Exception(String.format("Backup: %s not ready", basebackupCrName));
    }
    Optional<com.yugabyte.yw.models.Backup> optBkp =
        com.yugabyte.yw.models.Backup.maybeGet(
            customer.getUuid(), UUID.fromString(backup.getStatus().getResourceUUID()));
    if (!optBkp.isPresent()) {
      throw new Exception(String.format("Backup: %s object does not exist", basebackupCrName));
    }
    return optBkp.get();
  }

  public void createBackupCr(com.yugabyte.yw.models.Backup backup) throws Exception {
    UUID baseBackupUUID = backup.getBaseBackupUUID();
    BackupTableParams params = backup.getBackupInfo();

    // Backup Spec
    BackupSpec crSpec = new BackupSpec();
    if (params.backupType == TableType.PGSQL_TABLE_TYPE) {
      crSpec.setBackupType(BackupSpec.BackupType.PGSQL_TABLE_TYPE);
    } else if (params.backupType == TableType.YQL_TABLE_TYPE) {
      crSpec.setBackupType(BackupSpec.BackupType.YQL_TABLE_TYPE);
    } else {
      throw new Exception(
          String.format("Unsupported backup type: %s", params.backupType.toString()));
    }
    if (!params.isFullBackup()) {
      crSpec.setKeyspace(params.backupList.get(0).getKeyspace());
    }
    CustomerConfig storageConfig =
        CustomerConfig.get(backup.getCustomerUUID(), backup.getStorageConfigUUID());
    crSpec.setStorageConfig(storageConfig.getConfigName());
    crSpec.setTimeBeforeDelete(params.timeBeforeDelete);
    Universe universe =
        Universe.getOrBadRequest(backup.getUniverseUUID(), Customer.get(backup.getCustomerUUID()));
    crSpec.setUniverse(universe.getUniverseDetails().getKubernetesResourceDetails().name);
    // If incremental backup, add incemental backup base name in spec
    if (!baseBackupUUID.equals(backup.getBackupUUID())) {
      com.yugabyte.yw.models.Backup baseBackup =
          com.yugabyte.yw.models.Backup.getOrBadRequest(backup.getCustomerUUID(), baseBackupUUID);
      crSpec.setIncrementalBackupBase(
          baseBackup.getBackupInfo().getKubernetesResourceDetails().name);
    }

    // Metadata
    ObjectMetaBuilder metadataBuilder =
        new ObjectMetaBuilder()
            .withName(params.getKubernetesResourceDetails().name)
            .withNamespace(params.getKubernetesResourceDetails().namespace)
            .withLabels(Map.of(IGNORE_RECONCILER_ADD_LABEL, "true"))
            .withFinalizers(Collections.singletonList(YB_FINALIZER));
    if (!baseBackupUUID.equals(backup.getBackupUUID())) {
      com.yugabyte.yw.models.Backup lastSuccessfulbackup =
          com.yugabyte.yw.models.Backup.getLastSuccessfulBackupInChain(
              backup.getCustomerUUID(), baseBackupUUID);
      metadataBuilder.withOwnerReferences(
          Collections.singletonList(
              getResourceOwnerReference(
                  lastSuccessfulbackup.getBackupInfo().getKubernetesResourceDetails(),
                  Backup.class)));
    }

    Backup crBackup = new Backup();
    crBackup.setMetadata(metadataBuilder.build());
    crBackup.setSpec(crSpec);

    // Initial backup status
    BackupStatus crStatus = new BackupStatus();
    crStatus.setMessage("Adding scheduled backup");
    crStatus.setResourceUUID(backup.getBackupUUID().toString());
    crStatus.setTaskUUID(backup.getTaskUUID().toString());

    try (final KubernetesClient kubernetesClient = createKubernetesClient()) {
      kubernetesClient
          .resources(Backup.class)
          .inNamespace(params.getKubernetesResourceDetails().namespace)
          .resource(crBackup)
          .create();
      // Need to explicitly update status
      crBackup.setStatus(crStatus);
      kubernetesClient
          .resources(Backup.class)
          .inNamespace(params.getKubernetesResourceDetails().namespace)
          .resource(crBackup)
          .replaceStatus();
    } catch (Exception e) {
      throw new Exception(
          String.format(
              "Unable to add cr resource: %s type: Backup",
              params.getKubernetesResourceDetails().name),
          e);
    }
  }

  public void createProviderCrFromProviderEbean(
      KubernetesProviderFormData providerData, String namespace) {
    YBProvider providerCr = new YBProvider();
    providerCr.setMetadata(buildMetadata(providerData, namespace));
    providerCr.setSpec(buildSpec(providerData));

    try (final KubernetesClient client = createKubernetesClient()) {
      client.resources(YBProvider.class).inNamespace(namespace).resource(providerCr).create();
    } catch (Exception e) {
      throw new RuntimeException(
          String.format("Unable to add YBProvider CR: %s", providerData.name), e);
    }
  }

  private ObjectMeta buildMetadata(KubernetesProviderFormData data, String namespace) {
    return new ObjectMetaBuilder()
        .withName(data.name)
        .withNamespace(namespace)
        .withLabels(Map.of(AUTO_PROVIDER_LABEL, "true"))
        .withFinalizers(Collections.singletonList(YB_FINALIZER))
        .build();
  }

  private YBProviderSpec buildSpec(KubernetesProviderFormData providerData) {
    YBProviderSpec spec = new YBProviderSpec();
    spec.setCloudInfo(parseCloudInfoConfig(providerData.config));

    List<Regions> regions =
        providerData.regionList.stream()
            .map(
                regionData -> {
                  Regions region = new Regions();
                  region.setCode(regionData.code);
                  List<Zones> zones =
                      regionData.zoneList.stream()
                          .map(
                              zone -> {
                                Zones zoneSpec = new Zones();
                                zoneSpec.setCode(zone.code);
                                zoneSpec.setCloudInfo(parseZoneCloudInfoConfig(zone.config));
                                return zoneSpec;
                              })
                          .collect(Collectors.toList());
                  region.setZones(zones);
                  return region;
                })
            .collect(Collectors.toList());

    spec.setRegions(regions);
    return spec;
  }

  public void checkAndDeleteAutoCreatedProvider(YBProvider provider, String namespace) {
    try (final KubernetesClient kubernetesClient = createKubernetesClient()) {
      if (provider.getMetadata().getLabels().containsKey(AUTO_PROVIDER_LABEL)) {
        kubernetesClient
            .resources(YBProvider.class)
            .inNamespace(namespace)
            .resource(provider)
            .delete();
      }
    } catch (Exception e) {
      log.error("Exception in checking and deleting auto-created provider", e);
    }
  }

  public Optional<YBProvider> maybeGetCRForProvider(String providerName, String namespace) {
    try (final KubernetesClient client = createKubernetesClient()) {
      YBProvider cr =
          client.resources(YBProvider.class).inNamespace(namespace).withName(providerName).get();
      return Optional.ofNullable(cr);
    } catch (Exception e) {
      log.error(
          "Failed to fetch YBProvider CR for name={} in namespace={}", providerName, namespace, e);
      return Optional.empty();
    }
  }

  // Parse cloud info config from the provider ebean and converts it to the form required by the
  // operator.
  private io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo parseCloudInfoConfig(
      Map<String, String> cloudInfo) {
    io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo cloudInfoSpec =
        new io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo();
    cloudInfoSpec.setKubernetesProvider(
        io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo.KubernetesProvider.valueOf(
            cloudInfo.get("KUBECONFIG_PROVIDER").toUpperCase()));
    cloudInfoSpec.setKubernetesImageRegistry(cloudInfo.get("KUBECONFIG_IMAGE_REGISTRY"));
    return cloudInfoSpec;
  }

  private io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.CloudInfo
      parseZoneCloudInfoConfig(Map<String, String> cloudInfo) {
    io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.CloudInfo cloudInfoSpec =
        new io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.CloudInfo();
    cloudInfoSpec.setKubeDomain(cloudInfo.get("KUBE_DOMAIN"));
    cloudInfoSpec.setKubernetesStorageClass(cloudInfo.get("STORAGE_CLASS"));
    cloudInfoSpec.setKubeNamespace(cloudInfo.get("KUBENAMESPACE"));
    cloudInfoSpec.setKubePodAddressTemplate(cloudInfo.get("KUBE_POD_ADDRESS_TEMPLATE"));
    cloudInfoSpec.setCertManagerIssuerName(cloudInfo.get("CERT-MANAGER-ISSUER-NAME"));
    cloudInfoSpec.setCertManagerIssuerGroup(cloudInfo.get("CERT-MANAGER-ISSUER-GROUP"));
    cloudInfoSpec.setCertManagerIssuerKind(cloudInfo.get("CERT-MANAGER-ISSUER-KIND"));
    return cloudInfoSpec;
  }

  public Provider getProviderReqFromProviderDetails(JsonNode providerDetails) {
    return validatingFormFactory.getFormDataOrBadRequest(providerDetails, Provider.class);
  }

  public boolean hasRegionConfigChanged(
      List<Region> newRegionList, List<Region> existingRegionList) {
    // Early exit if sizes don't match
    if (newRegionList.size() != existingRegionList.size()) {
      log.info(
          "Region count changed: new={}, existing={}",
          newRegionList.size(),
          existingRegionList.size());
      return true;
    }

    for (Region newRegion : newRegionList) {
      Region existingRegion = findByCode(existingRegionList, newRegion.getCode(), Region::getCode);

      // If region doesn't exist, we need to add it
      if (existingRegion == null) {
        log.info("New region found: {}", newRegion.getCode());
        return true;
      }

      // Check if zone count changed
      if (newRegion.getZones().size() != existingRegion.getZones().size()) {
        log.info(
            "Zone count changed for region {}: new={}, existing={}",
            newRegion.getCode(),
            newRegion.getZones().size(),
            existingRegion.getZones().size());
        return true;
      }

      for (AvailabilityZone newZone : newRegion.getZones()) {
        AvailabilityZone existingZone =
            findByCode(existingRegion.getZones(), newZone.getCode(), AvailabilityZone::getCode);
        // If zone doesn't exist, we need to add it
        if (existingZone == null) {
          log.info("New zone found: {} in region {}", newZone.getCode(), newRegion.getCode());
          return true;
        }
        if (hasZoneConfigChanged(
            CloudInfoInterface.fetchEnvVars(newZone),
            CloudInfoInterface.fetchEnvVars(existingZone))) {
          log.info("Zone config changed for zone {}", newZone.getCode());
          return true;
        }
      }
    }
    return false;
  }

  private <T> T findByCode(List<T> items, String code, Function<T, String> codeExtractor) {
    for (T item : items) {
      if (code.equals(codeExtractor.apply(item))) {
        return item;
      }
    }
    return null;
  }

  private boolean hasZoneConfigChanged(
      Map<String, String> newZone, Map<String, String> existingZone) {
    for (String key : ZONE_CONFIG_KEYS_TO_CHECK) {
      String newValue = newZone.getOrDefault(key, "");
      String existingValue = existingZone.getOrDefault(key, "");
      if (!Objects.equals(newValue, existingValue)) {
        return true;
      }
    }
    // Finally check if the kubeconfig has changed
    return hasKubeConfigChanged(existingZone, newZone);
  }

  public boolean hasKubeConfigChanged(
      Map<String, String> existingCloudInfo, Map<String, String> desiredCloudInfo) {
    // Look for KUBECONFIG in the existing kubeconfig and extract the name of the kubeconfig file
    // since we don't store the kubeconfig name in the provider.
    String existingKubeConfigName =
        extractKubeConfigName(existingCloudInfo.getOrDefault("KUBECONFIG", ""));
    String desiredKubeConfigName = desiredCloudInfo.getOrDefault("KUBECONFIG_NAME", "");
    return !Objects.equals(existingKubeConfigName, desiredKubeConfigName);
  }

  private String extractKubeConfigName(String kubeConfigPath) {
    if (StringUtils.isBlank(kubeConfigPath)) {
      return "";
    }
    return Paths.get(kubeConfigPath).getFileName().toString();
  }
}
