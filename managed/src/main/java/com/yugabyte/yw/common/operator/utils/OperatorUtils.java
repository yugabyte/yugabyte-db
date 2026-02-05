package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
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
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.ResourceTracker;
import com.yugabyte.yw.common.operator.YBUniverseReconciler;
import com.yugabyte.yw.common.operator.helpers.KubernetesOverridesSerializer;
import com.yugabyte.yw.common.operator.helpers.OperatorPlacementInfoHelper;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse.ThrottleParamValue;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
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
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.BackupSchedule;
import io.yugabyte.operator.v1alpha1.BackupScheduleSpec;
import io.yugabyte.operator.v1alpha1.BackupSpec;
import io.yugabyte.operator.v1alpha1.BackupStatus;
import io.yugabyte.operator.v1alpha1.DrConfig;
import io.yugabyte.operator.v1alpha1.PitrConfig;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.ReleaseSpec;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.StorageConfigSpec;
import io.yugabyte.operator.v1alpha1.StorageConfigStatus;
import io.yugabyte.operator.v1alpha1.YBProvider;
import io.yugabyte.operator.v1alpha1.YBProviderSpec;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import io.yugabyte.operator.v1alpha1.YBUniverseStatus;
import io.yugabyte.operator.v1alpha1.releasespec.config.DownloadConfig;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.Gcs;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.Http;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.S3;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.gcs.CredentialsJsonSecret;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.s3.SecretAccessKeySecret;
import io.yugabyte.operator.v1alpha1.storageconfigspec.AwsSecretAccessKeySecret;
import io.yugabyte.operator.v1alpha1.storageconfigspec.AzureStorageSasTokenSecret;
import io.yugabyte.operator.v1alpha1.storageconfigspec.Data;
import io.yugabyte.operator.v1alpha1.storageconfigspec.GcsCredentialsJsonSecret;
import io.yugabyte.operator.v1alpha1.ybproviderspec.Regions;
import io.yugabyte.operator.v1alpha1.ybproviderspec.regions.Zones;
import io.yugabyte.operator.v1alpha1.ybuniversespec.ReadReplica;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YbcThrottleParameters;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
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
import org.yb.client.YBClient;
import play.libs.Json;

@Slf4j
public class OperatorUtils {

  public static final String IGNORE_RECONCILER_ADD_LABEL = "ignore-reconciler-add";
  public static final String YB_FINALIZER = "finalizer.k8soperator.yugabyte.com";
  public static final String AUTO_PROVIDER_LABEL = "auto-provider";
  public static final int KUBERNETES_NAME_MAX_LENGTH = 63;
  public static final String PROVIDER_KUBECONFIG_KEY = "PROVIDER_KUBECONFIG";

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
  private final YBClientService ybService;
  private final KubernetesClientFactory kubernetesClientFactory;
  private final UniverseImporter universeImporter;
  private final KubernetesManagerFactory kubernetesManagerFactory;
  private Config _k8sClientConfig;
  private ReleaseManager releaseManager;
  private ObjectMapper objectMapper;

  @Inject
  public OperatorUtils(
      RuntimeConfGetter confGetter,
      ReleaseManager releaseManager,
      YbcManager ybcManager,
      ValidatingFormFactory validatingFormFactory,
      YBClientService ybService,
      KubernetesClientFactory kubernetesClientFactory,
      UniverseImporter universeImporter,
      KubernetesManagerFactory kubernetesManagerFactory) {
    this.releaseManager = releaseManager;
    this.confGetter = confGetter;
    this.ybcManager = ybcManager;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
    namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    this.validatingFormFactory = validatingFormFactory;
    this.kubernetesClientFactory = kubernetesClientFactory;
    this.objectMapper = new ObjectMapper();
    this.ybService = ybService;
    this.universeImporter = universeImporter;
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
    String name = YBUniverseReconciler.getUniverseName(ybUniverse);
    log.debug("Getting universe from name: {}", name);
    Optional<Universe> universe = Universe.maybeGetUniverseByName(customerId, name);
    if (universe.isPresent()) {
      return universe.get();
    }
    return null;
  }

  public YBUniverse getYBUniverse(KubernetesResourceDetails name) throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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

  public static String kubernetesCompatName(String name) {
    String newName = name.replace("_", "-");
    newName = newName.replace(" ", "-");
    newName = newName.toLowerCase();
    return newName;
  }

  /**
   * Extracts the YBA resource ID from Kubernetes resource metadata annotations.
   *
   * @param metadata The ObjectMeta from a Kubernetes resource
   * @return The UUID if the annotation exists and is valid, null otherwise
   */
  public static UUID getYbaResourceId(ObjectMeta metadata) {
    if (metadata == null || metadata.getAnnotations() == null) {
      return null;
    }
    Map<String, String> annotations = metadata.getAnnotations();
    if (annotations == null) {
      return null;
    }
    String resourceId = annotations.get(ResourceAnnotationKeys.YBA_RESOURCE_ID);
    if (resourceId == null || resourceId.isEmpty()) {
      return null;
    }
    try {
      return UUID.fromString(resourceId);
    } catch (IllegalArgumentException e) {
      log.warn("Invalid YBA resource ID in annotation: {}. Expected UUID format.", resourceId, e);
      return null;
    }
  }

  /**
   * Checks if a Kubernetes resource has a YBA resource ID annotation.
   *
   * @param metadata The ObjectMeta from a Kubernetes resource
   * @return true if the annotation exists, false otherwise
   */
  public static boolean hasYbaResourceId(ObjectMeta metadata) {
    return getYbaResourceId(metadata) != null;
  }

  /*--- YBUniverse related help methods ---*/

  public boolean shouldUpdatePrimaryCluster(Cluster currentCluster, YBUniverse newYbUniverse) {
    UserIntent currentUserIntent = currentCluster.userIntent;
    int newNumNodes = newYbUniverse.getSpec().getNumNodes().intValue();
    DeviceInfo newDeviceInfo = mapDeviceInfo(newYbUniverse.getSpec().getDeviceInfo());
    DeviceInfo newMasterDeviceInfo =
        mapMasterDeviceInfo(newYbUniverse.getSpec().getMasterDeviceInfo());
    K8SNodeResourceSpec newMasterK8SNodeResourceSpec =
        toNodeResourceSpec(
            newYbUniverse.getSpec().getMasterResourceSpec(), s -> s.getCpu(), s -> s.getMemory());
    K8SNodeResourceSpec newTserverK8SNodeResourceSpec =
        toNodeResourceSpec(
            newYbUniverse.getSpec().getTserverResourceSpec(), s -> s.getCpu(), s -> s.getMemory());
    return !(currentUserIntent.numNodes == newNumNodes)
        || checkifDeviceInfoChanged(
            currentUserIntent.deviceInfo, newDeviceInfo.volumeSize.intValue())
        || checkifDeviceInfoChanged(
            currentUserIntent.masterDeviceInfo, newMasterDeviceInfo.volumeSize.intValue())
        || OperatorPlacementInfoHelper.checkIfPlacementInfoChanged(
            currentCluster.placementInfo, newYbUniverse, false)
        || !currentUserIntent.masterK8SNodeResourceSpec.equals(newMasterK8SNodeResourceSpec)
        || !currentUserIntent.tserverK8SNodeResourceSpec.equals(newTserverK8SNodeResourceSpec);
  }

  public boolean shouldAddReadReplica(Universe universe, YBUniverse ybUniverse) {
    int readReplicaClusterCount = universe.getUniverseDetails().getReadOnlyClusters().size();
    boolean hasReadReplica = ybUniverse.getSpec().getReadReplica() != null;
    return readReplicaClusterCount == 0 && hasReadReplica;
  }

  public boolean shouldRemoveReadReplica(Universe universe, YBUniverse ybUniverse) {
    int readReplicaClusterCount = universe.getUniverseDetails().getReadOnlyClusters().size();
    boolean hasReadReplica = ybUniverse.getSpec().getReadReplica() != null;
    return readReplicaClusterCount > 0 && !hasReadReplica;
  }

  public boolean shouldUpdateReadReplica(Universe universe, YBUniverse ybUniverse) {
    int readReplicaClusterCount = universe.getUniverseDetails().getReadOnlyClusters().size();
    boolean hasReadReplica = ybUniverse.getSpec().getReadReplica() != null;
    // No read replica exists or none requested
    if (readReplicaClusterCount == 0 || !hasReadReplica) {
      return false;
    } else {
      UserIntent readReplicaUserIntent =
          universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent;
      PlacementInfo readReplicaPlacementInfo =
          universe.getUniverseDetails().getReadOnlyClusters().get(0).placementInfo;
      K8SNodeResourceSpec newReadReplicaTserverK8SNodeResourceSpec =
          toNodeResourceSpec(
              ybUniverse.getSpec().getReadReplica().getTserverResourceSpec(),
              s -> s.getCpu(),
              s -> s.getMemory());
      return readReplicaUserIntent.numNodes
              != ybUniverse.getSpec().getReadReplica().getNumNodes().intValue()
          || readReplicaUserIntent.replicationFactor
              != ybUniverse.getSpec().getReadReplica().getReplicationFactor().intValue()
          || OperatorPlacementInfoHelper.checkIfPlacementInfoChanged(
              readReplicaPlacementInfo, ybUniverse, true)
          || checkifDeviceInfoChanged(
              readReplicaUserIntent.deviceInfo,
              ybUniverse.getSpec().getReadReplica().getDeviceInfo().getVolumeSize().intValue())
          || !readReplicaUserIntent.tserverK8SNodeResourceSpec.equals(
              newReadReplicaTserverK8SNodeResourceSpec);
    }
  }

  public boolean checkifDeviceInfoChanged(DeviceInfo oldDeviceInfo, int newVolumeSize) {
    return !oldDeviceInfo.volumeSize.equals(newVolumeSize);
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

  public <T> K8SNodeResourceSpec toNodeResourceSpec(
      T operatorNodeResourceSpec, Function<T, Double> cpuMapper, Function<T, Double> memoryMapper) {
    K8SNodeResourceSpec spec = new K8SNodeResourceSpec();

    if (operatorNodeResourceSpec == null) {
      return spec;
    }

    Double cpu = cpuMapper.apply(operatorNodeResourceSpec);
    if (cpu != null) {
      spec.cpuCoreCount = cpu;
    }

    Double memory = memoryMapper.apply(operatorNodeResourceSpec);
    if (memory != null) {
      spec.memoryGib = memory;
    }

    return spec;
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
          return shouldUpdatePrimaryCluster(u.getUniverseDetails().getPrimaryCluster(), ybUniverse)
              || shouldUpdateReadReplica(u, ybUniverse);
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
            || shouldUpdatePrimaryCluster(u.getUniverseDetails().getPrimaryCluster(), ybUniverse);
    log.trace("primary cluster mismatch: {}", mismatch);
    mismatch =
        mismatch || shouldAddReadReplica(u, ybUniverse) || shouldRemoveReadReplica(u, ybUniverse);
    log.trace("read replica count mismatch: {}", mismatch);
    mismatch = mismatch || shouldUpdateReadReplica(u, ybUniverse);
    log.trace("read replica mismatch: {}", mismatch);
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
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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

  public String getAndParseSecretForKey(
      String name,
      @Nullable String namespace,
      String key,
      ResourceTracker resourceTracker,
      KubernetesResourceDetails owner) {
    Secret secret = getSecret(name, namespace);
    if (secret == null) {
      log.warn("Secret {} not found", name);
      return null;
    }
    resourceTracker.trackDependency(owner, secret);
    log.trace("Tracking secret {} as dependency of {}", secret.getMetadata().getName(), owner);
    return parseSecretForKey(secret, key);
  }

  public Secret getSecret(String name, @Nullable String namespace) {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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
          case GFlagsUtil.YBC_DISK_READ_BYTES_PER_SECOND:
            if (value != specParams.getDiskReadBytesPerSec()) return true;
            break;
          case GFlagsUtil.YBC_DISK_WRITE_BYTES_PER_SECOND:
            if (value != specParams.getDiskWriteBytesPerSec()) return true;
            break;
          default:
            // This should only happen if a new throttle parameter is introduced and not added here.
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
    if (backupSchedule.getSpec().getName() != null) {
      backupRequestParams.scheduleName = kubernetesCompatName(backupSchedule.getSpec().getName());
    }
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
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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

  public void createBackupCr(com.yugabyte.yw.models.Backup backup, String storageConfigName)
      throws Exception {
    createBackupCr(backup, storageConfigName, null);
  }

  public void createBackupCr(
      com.yugabyte.yw.models.Backup backup, String storageConfigName, @Nullable String namespace)
      throws Exception {
    UUID baseBackupUUID = backup.getBaseBackupUUID();
    BackupTableParams params = backup.getBackupInfo();

    if (params.getKubernetesResourceDetails() == null) {
      params.setKubernetesResourceDetails(
          new KubernetesResourceDetails(
              String.format("backup-%s", backup.getBackupUUID()), namespace));
    }

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
    crSpec.setStorageConfig(storageConfigName);
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

    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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
      KubernetesProviderFormData providerData, String namespace, boolean isAutoCreated) {
    createProviderCrFromProviderEbean(providerData, namespace, null, isAutoCreated, null);
  }

  public void createProviderCrFromProviderEbean(
      KubernetesProviderFormData providerData,
      String namespace,
      UUID providerUUID,
      boolean isAutoCreated,
      Map<String, String> secretMap) {
    try (final KubernetesClient client =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      if (client
              .resources(YBProvider.class)
              .inNamespace(namespace)
              .withName(kubernetesCompatName(providerData.name))
              .get()
          != null) {
        log.info("Provider {} already exists, skipping creation", providerData.name);
        return;
      }
      YBProvider providerCr = new YBProvider();
      providerCr.setMetadata(buildMetadata(providerData, namespace, providerUUID, isAutoCreated));
      providerCr.setSpec(buildSpec(providerData, secretMap, namespace));
      client.resources(YBProvider.class).inNamespace(namespace).resource(providerCr).create();
    } catch (Exception e) {
      throw new RuntimeException(
          String.format("Unable to add YBProvider CR: %s", providerData.name), e);
    }
  }

  private ObjectMeta buildMetadata(
      KubernetesProviderFormData data, String namespace, UUID providerUUID, boolean isAutoCreated) {
    ObjectMetaBuilder metadataBuilder =
        new ObjectMetaBuilder()
            .withName(kubernetesCompatName(data.name))
            .withNamespace(namespace)
            .withLabels(Map.of(AUTO_PROVIDER_LABEL, String.valueOf(isAutoCreated)))
            .withFinalizers(Collections.singletonList(YB_FINALIZER));
    if (providerUUID != null) {
      metadataBuilder.withAnnotations(
          Map.ofEntries(
              Map.entry(ResourceAnnotationKeys.YBA_RESOURCE_ID, providerUUID.toString())));
    }
    return metadataBuilder.build();
  }

  private YBProviderSpec buildSpec(
      KubernetesProviderFormData providerData, Map<String, String> secretMap, String namespace) {
    YBProviderSpec spec = new YBProviderSpec();
    String secretName = null;
    if (secretMap != null && secretMap.containsKey(PROVIDER_KUBECONFIG_KEY)) {
      secretName = secretMap.get(PROVIDER_KUBECONFIG_KEY);
    }
    spec.setCloudInfo(parseCloudInfoConfig(providerData.config, secretName, namespace));

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
                                String secretNameZone = null;
                                if (secretMap != null && secretMap.containsKey(zone.code)) {
                                  secretNameZone = secretMap.get(zone.code);
                                }
                                Zones zoneSpec = new Zones();
                                zoneSpec.setCode(zone.code);
                                zoneSpec.setCloudInfo(
                                    parseZoneCloudInfoConfig(
                                        zone.config, secretNameZone, namespace));
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
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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
    try (final KubernetesClient client =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
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
      Map<String, String> cloudInfo, @Nullable String secretName, String namespace) {
    io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo cloudInfoSpec =
        new io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo();
    cloudInfoSpec.setKubernetesProvider(
        io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo.KubernetesProvider.valueOf(
            cloudInfo.get("KUBECONFIG_PROVIDER").toUpperCase()));
    cloudInfoSpec.setKubernetesImageRegistry(cloudInfo.get("KUBECONFIG_IMAGE_REGISTRY"));
    if (secretName != null && !secretName.isEmpty()) {
      io.yugabyte.operator.v1alpha1.ybproviderspec.cloudinfo.KubeConfigSecret kubeConfigSecret =
          new io.yugabyte.operator.v1alpha1.ybproviderspec.cloudinfo.KubeConfigSecret();
      kubeConfigSecret.setName(secretName);
      kubeConfigSecret.setNamespace(namespace);
      cloudInfoSpec.setKubeConfigSecret(kubeConfigSecret);
    }
    return cloudInfoSpec;
  }

  private io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.CloudInfo
      parseZoneCloudInfoConfig(
          Map<String, String> cloudInfo, @Nullable String secretName, String namespace) {
    io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.CloudInfo cloudInfoSpec =
        new io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.CloudInfo();
    cloudInfoSpec.setKubeDomain(cloudInfo.get("KUBE_DOMAIN"));
    cloudInfoSpec.setKubernetesStorageClass(cloudInfo.get("STORAGE_CLASS"));
    cloudInfoSpec.setKubeNamespace(cloudInfo.get("KUBENAMESPACE"));
    cloudInfoSpec.setKubePodAddressTemplate(cloudInfo.get("KUBE_POD_ADDRESS_TEMPLATE"));
    cloudInfoSpec.setCertManagerIssuerName(cloudInfo.get("CERT-MANAGER-ISSUER-NAME"));
    cloudInfoSpec.setCertManagerIssuerGroup(cloudInfo.get("CERT-MANAGER-ISSUER-GROUP"));
    cloudInfoSpec.setCertManagerIssuerKind(cloudInfo.get("CERT-MANAGER-ISSUER-KIND"));
    if (secretName != null && !secretName.isEmpty()) {
      io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.cloudinfo.KubeConfigSecret
          kubeConfigSecret =
              new io.yugabyte.operator.v1alpha1.ybproviderspec.regions.zones.cloudinfo
                  .KubeConfigSecret();
      kubeConfigSecret.setName(secretName);
      kubeConfigSecret.setNamespace(namespace);
      cloudInfoSpec.setKubeConfigSecret(kubeConfigSecret);
    }
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

  public DrConfigCreateForm getDrConfigCreateFormFromCr(
      DrConfig drConfig, SharedIndexInformer<StorageConfig> scInformer) throws Exception {
    ObjectNode crParams = objectMapper.valueToTree(drConfig.getSpec());
    DrConfigCreateForm drConfigCreateForm =
        getDrConfigCreateFormFromCr(crParams, drConfig.getMetadata().getNamespace(), scInformer);
    drConfigCreateForm.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(drConfig));
    return drConfigCreateForm;
  }

  @VisibleForTesting
  DrConfigCreateForm getDrConfigCreateFormFromCr(
      ObjectNode crParams, String namespace, SharedIndexInformer<StorageConfig> scInformer)
      throws Exception {

    Customer cust = getOperatorCustomer();
    String crSourceUniverseName = crParams.get("sourceUniverse").asText();
    Universe sourceUniverse =
        getUniverseFromNameAndNamespace(cust.getId(), crSourceUniverseName, namespace);
    if (sourceUniverse == null) {
      throw new Exception("No universe found with name " + crSourceUniverseName);
    }
    UUID sourceUniverseUUID = sourceUniverse.getUniverseUUID();

    String crTargetUniverseName = crParams.get("targetUniverse").asText();
    Universe targetUniverse =
        getUniverseFromNameAndNamespace(cust.getId(), crTargetUniverseName, namespace);
    if (targetUniverse == null) {
      throw new Exception("No universe found with name " + crTargetUniverseName);
    }
    UUID targetUniverseUUID = targetUniverse.getUniverseUUID();

    String crStorageConfig = crParams.get("storageConfig").asText();
    UUID storageConfigUUID = getStorageConfigUUIDFromName(crStorageConfig, scInformer);
    if (storageConfigUUID == null) {
      throw new Exception("No storage config found with name " + crStorageConfig);
    }

    String drConfigName = crParams.get("name").asText();

    TableType tableType = TableType.PGSQL_TABLE_TYPE;
    YBClient client = ybService.getUniverseClient(sourceUniverse);
    Map<String, String> namespaceNameNamespaceIdMap =
        UniverseTaskBase.getKeyspaceNameKeyspaceIdMap(client, tableType);
    JsonNode databasesNode = crParams.get("databases");
    ArrayNode dbsArray = JsonNodeFactory.instance.arrayNode();

    if (databasesNode != null && databasesNode.isArray()) {
      for (JsonNode dbNode : databasesNode) {
        String namespaceName = dbNode.asText().trim();
        String namespaceId = namespaceNameNamespaceIdMap.get(namespaceName);
        if (namespaceId == null) {
          throw new IllegalArgumentException(
              String.format(
                  "A namespace with name '%s' and table type '%s' could not be found",
                  namespaceName, tableType.name()));
        }
        dbsArray.add(namespaceId);
      }
    }

    crParams.put("sourceUniverseUUID", sourceUniverseUUID.toString());
    crParams.put("targetUniverseUUID", targetUniverseUUID.toString());
    crParams.put("storageConfigUUID", storageConfigUUID.toString());
    crParams.put("name", drConfigName);
    crParams.put("dbs", dbsArray);

    DrConfigCreateForm createForm =
        validatingFormFactory.getFormDataOrBadRequest(crParams, DrConfigCreateForm.class);
    BootstrapParams.BootstrapBackupParams backupRequestParams =
        new BootstrapParams.BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = storageConfigUUID;
    createForm.bootstrapParams = new RestartBootstrapParams();
    createForm.bootstrapParams.backupRequestParams = backupRequestParams;

    return createForm;
  }

  public DrConfigSetDatabasesForm getDrConfigSetDatabasesFormFromCr(
      DrConfig drConfig, SharedIndexInformer<StorageConfig> scInformer) throws Exception {
    JsonNode crParams = objectMapper.valueToTree(drConfig.getSpec());
    DrConfigSetDatabasesForm drConfigSetDatabasesForm =
        getDrConfigSetDatabasesFormFromCr(
            crParams, drConfig.getMetadata().getNamespace(), scInformer);
    drConfigSetDatabasesForm.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(drConfig));
    return drConfigSetDatabasesForm;
  }

  @VisibleForTesting
  DrConfigSetDatabasesForm getDrConfigSetDatabasesFormFromCr(
      JsonNode crParams, String namespace, SharedIndexInformer<StorageConfig> scInformer)
      throws Exception {
    Customer cust = getOperatorCustomer();
    String crSourceUniverseName = ((ObjectNode) crParams).get("sourceUniverse").asText();
    Universe sourceUniverse =
        getUniverseFromNameAndNamespace(cust.getId(), crSourceUniverseName, namespace);
    if (sourceUniverse == null) {
      throw new Exception("No universe found with name " + crSourceUniverseName);
    }
    TableType tableType = TableType.PGSQL_TABLE_TYPE;
    YBClient client = ybService.getUniverseClient(sourceUniverse);
    Map<String, String> namespaceNameNamespaceIdMap =
        UniverseTaskBase.getKeyspaceNameKeyspaceIdMap(client, tableType);
    JsonNode databasesNode = ((ObjectNode) crParams).get("databases");
    ArrayNode dbsArray = JsonNodeFactory.instance.arrayNode();
    if (databasesNode != null && databasesNode.isArray()) {
      for (JsonNode dbNode : databasesNode) {
        String namespaceName = dbNode.asText().trim();
        String namespaceId = namespaceNameNamespaceIdMap.get(namespaceName);
        if (namespaceId == null) {
          throw new IllegalArgumentException(
              String.format(
                  "A namespace with name '%s' and table type '%s' could not be found",
                  namespaceName, tableType.name()));
        }
        dbsArray.add(namespaceId);
      }
    }

    ((ObjectNode) crParams).put("dbs", dbsArray);

    return validatingFormFactory.getFormDataOrBadRequest(crParams, DrConfigSetDatabasesForm.class);
  }

  /**
   * Creates a Kubernetes Secret custom resource in the specified namespace. The method
   * automatically base64-encodes the provided value and creates an Opaque type secret with the
   * given key-value pair.
   *
   * @param name The name of the secret resource
   * @param namespace The Kubernetes namespace where the secret will be created
   * @param key The key under which the value will be stored in the secret
   * @param value The raw value to be stored (will be automatically base64-encoded)
   * @throws Exception If the secret creation fails or if there's an issue with the Kubernetes
   *     client
   */
  public void createSecretCr(String name, String namespace, String key, String value)
      throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      if (kubernetesClient.secrets().inNamespace(namespace).withName(name).get() != null) {
        log.info("Secret {} already exists, skipping creation", name);
        return;
      }
      Secret secret = new Secret();
      secret.setMetadata(new ObjectMetaBuilder().withName(name).withNamespace(namespace).build());
      secret.setData(
          Collections.singletonMap(key, Base64.getEncoder().encodeToString(value.getBytes())));
      secret.setType("Opaque");
      kubernetesClient.secrets().inNamespace(namespace).resource(secret).create();
    } catch (Exception e) {
      throw new Exception(String.format("Unable to create secret: %s type: Secret", name), e);
    }
  }

  public void createReleaseCr(
      com.yugabyte.yw.models.Release ybRelease,
      ReleaseArtifact k8sArtifact,
      ReleaseArtifact x86_64Artifact,
      String namespace,
      @Nullable String secretName)
      throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      if (kubernetesClient
              .resources(Release.class)
              .inNamespace(namespace)
              .withName(ybRelease.getVersion())
              .get()
          != null) {
        log.info("Release {} already exists, skipping creation", ybRelease.getVersion());
        return;
      }
      Release release = new Release();
      release.setMetadata(
          new ObjectMetaBuilder()
              .withName(ybRelease.getVersion())
              .withNamespace(namespace)
              .withAnnotations(
                  Map.ofEntries(
                      Map.entry(
                          ResourceAnnotationKeys.YBA_RESOURCE_ID,
                          ybRelease.getReleaseUUID().toString())))
              .build());
      ReleaseSpec releaseSpec = new ReleaseSpec();
      io.yugabyte.operator.v1alpha1.releasespec.Config config =
          new io.yugabyte.operator.v1alpha1.releasespec.Config();
      config.setVersion(ybRelease.getVersion());
      DownloadConfig downloadConfig = new DownloadConfig();
      if (k8sArtifact.getS3File() != null) {
        S3 s3 = new S3();
        s3.setPaths(new io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.s3.Paths());
        s3.getPaths().setHelmChart(k8sArtifact.getS3File().path);
        s3.getPaths().setHelmChartChecksum(k8sArtifact.getFormattedSha256());
        s3.getPaths().setX86_64(x86_64Artifact.getS3File().path);
        s3.getPaths().setX86_64_checksum(x86_64Artifact.getFormattedSha256());
        s3.setSecretAccessKey(k8sArtifact.getS3File().secretAccessKey);
        if (secretName != null) {
          SecretAccessKeySecret secret = new SecretAccessKeySecret();
          secret.setName(secretName);
          secret.setNamespace(namespace);
          s3.setSecretAccessKeySecret(secret);
        }
        downloadConfig.setS3(s3);
      } else if (k8sArtifact.getGcsFile() != null) {
        Gcs gcs = new Gcs();
        gcs.setPaths(
            new io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.gcs.Paths());
        gcs.getPaths().setHelmChart(k8sArtifact.getGcsFile().path);
        gcs.getPaths().setHelmChartChecksum(k8sArtifact.getFormattedSha256());
        gcs.getPaths().setX86_64(x86_64Artifact.getGcsFile().path);
        gcs.getPaths().setX86_64_checksum(x86_64Artifact.getFormattedSha256());
        if (secretName != null) {
          CredentialsJsonSecret secret = new CredentialsJsonSecret();
          secret.setName(secretName);
          secret.setNamespace(namespace);
          gcs.setCredentialsJsonSecret(secret);
        }
        downloadConfig.setGcs(gcs);
      } else if (k8sArtifact.getPackageURL() != null) {
        Http http = new Http();
        http.setPaths(
            new io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.http.Paths());
        http.getPaths().setHelmChart(k8sArtifact.getPackageURL());
        http.getPaths().setHelmChartChecksum(k8sArtifact.getFormattedSha256());
        http.getPaths().setX86_64(x86_64Artifact.getPackageURL());
        http.getPaths().setX86_64_checksum(x86_64Artifact.getFormattedSha256());
        downloadConfig.setHttp(http);
      } else {
        log.info("Release {} uses a local file", ybRelease.getVersion());
      }
      config.setDownloadConfig(downloadConfig);

      releaseSpec.setConfig(config);
      release.setSpec(releaseSpec);

      kubernetesClient.resources(Release.class).inNamespace(namespace).resource(release).create();
    }
  }

  public void createStorageConfigCr(
      CustomerConfig cfg, String namespace, @Nullable String secretName) throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      if (kubernetesClient
              .resources(StorageConfig.class)
              .inNamespace(namespace)
              .withName(kubernetesCompatName(cfg.getConfigName()))
              .get()
          != null) {
        log.info("Storage config {} already exists, skipping creation", cfg.getName());
        return;
      }
      StorageConfig storageConfig = new StorageConfig();
      storageConfig.setMetadata(
          new ObjectMetaBuilder()
              .withName(kubernetesCompatName(cfg.getConfigName()))
              .withNamespace(namespace)
              .withAnnotations(
                  Map.ofEntries(
                      Map.entry(
                          ResourceAnnotationKeys.YBA_RESOURCE_ID, cfg.getConfigUUID().toString())))
              .build());
      StorageConfigSpec spec = new StorageConfigSpec();
      spec.setName(cfg.getConfigName());
      CustomerConfigData data = cfg.getDataObject();
      Data specData = new Data();
      switch (cfg.getName()) {
        case CustomerConfigConsts.NAME_S3:
          spec.setConfig_type(StorageConfigSpec.Config_type.STORAGE_S3);
          CustomerConfigStorageS3Data s3Data = (CustomerConfigStorageS3Data) data;
          specData.setAWS_ACCESS_KEY_ID(s3Data.awsAccessKeyId);
          if (s3Data.awsHostBase != null) {
            specData.setAWS_HOST_BASE(s3Data.awsHostBase);
          }
          specData.setUSE_IAM(s3Data.isIAMInstanceProfile);
          specData.setBACKUP_LOCATION(s3Data.backupLocation);
          specData.setPATH_STYLE_ACCESS(s3Data.isPathStyleAccess);
          if (secretName != null) {
            AwsSecretAccessKeySecret secret = new AwsSecretAccessKeySecret();
            secret.setName(secretName);
            secret.setNamespace(namespace);
            spec.setAwsSecretAccessKeySecret(secret);
          }
          break;
        case CustomerConfigConsts.NAME_GCS:
          spec.setConfig_type(StorageConfigSpec.Config_type.STORAGE_GCS);
          CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) data;
          specData.setUSE_IAM(gcsData.useGcpIam);
          specData.setBACKUP_LOCATION(gcsData.backupLocation);
          if (secretName != null) {
            GcsCredentialsJsonSecret secret = new GcsCredentialsJsonSecret();
            secret.setName(secretName);
            secret.setNamespace(namespace);
            spec.setGcsCredentialsJsonSecret(secret);
          }
          break;
        case CustomerConfigConsts.NAME_NFS:
          spec.setConfig_type(StorageConfigSpec.Config_type.STORAGE_NFS);
          CustomerConfigStorageNFSData nfsData = (CustomerConfigStorageNFSData) data;
          specData.setBACKUP_LOCATION(nfsData.backupLocation);
          break;
        case CustomerConfigConsts.NAME_AZURE:
          spec.setConfig_type(StorageConfigSpec.Config_type.STORAGE_AZ);
          CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) data;
          specData.setBACKUP_LOCATION(azData.backupLocation);
          if (secretName != null) {
            AzureStorageSasTokenSecret secret = new AzureStorageSasTokenSecret();
            secret.setName(secretName);
            secret.setNamespace(namespace);
            spec.setAzureStorageSasTokenSecret(secret);
          }
          break;
        default:
          throw new Exception(String.format("Unsupported storage config type: %s", cfg.getName()));
      }
      spec.setData(specData);
      storageConfig.setSpec(spec);
      StorageConfigStatus status = new StorageConfigStatus();
      status.setResourceUUID(cfg.getConfigUUID().toString());
      storageConfig.setStatus(status);
      kubernetesClient
          .resources(StorageConfig.class)
          .inNamespace(namespace)
          .resource(storageConfig)
          .create();
    } catch (Exception e) {
      throw new Exception(
          String.format("Unable to create storage config: %s type: StorageConfig", cfg.getName()),
          e);
    }
  }

  private long convertFrequencyToMillis(long frequency, TimeUnit timeUnit) {
    switch (timeUnit) {
      case DAYS:
        return frequency * 24 * 60 * 60 * 1000;
      case HOURS:
        return frequency * 60 * 60 * 1000;
      case MINUTES:
        return frequency * 60 * 1000;
      case SECONDS:
        return frequency * 1000;
      case MILLISECONDS:
        return frequency;
      case NANOSECONDS:
        return frequency / 1000000;
      case MICROSECONDS:
        return frequency / 1000;
        // TODO: how do we want to handle months and years?
      case MONTHS:
        log.warn("Months are not accurately supported for scheduling frequency, assuming 30 days");
        return frequency * 30 * 24 * 60 * 60 * 1000; // Assume 30 days per month
      case YEARS:
        log.warn("Years are not accurately supported for scheduling frequency, assuming 365 days");
        return frequency * 365 * 24 * 60 * 60 * 1000; // Assume 365 per year
      default:
        throw new RuntimeException("Unknown time unit: " + timeUnit);
    }
  }

  public void createBackupScheduleCr(
      Schedule ybBackupSchedule, String name, String storageConfigName, @Nullable String namespace)
      throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      if (kubernetesClient
              .resources(BackupSchedule.class)
              .inNamespace(namespace)
              .withName(name)
              .get()
          != null) {
        log.info("Backup schedule {} already exists, skipping creation", name);
        return;
      }
      BackupRequestParams params =
          Json.mapper().convertValue(ybBackupSchedule.getTaskParams(), BackupRequestParams.class);
      BackupSchedule backupSchedule = new BackupSchedule();
      backupSchedule.setMetadata(
          new ObjectMetaBuilder()
              .withName(name)
              .withNamespace(namespace)
              .withAnnotations(
                  Map.ofEntries(
                      Map.entry(
                          ResourceAnnotationKeys.YBA_RESOURCE_ID,
                          ybBackupSchedule.getScheduleUUID().toString())))
              .build());
      BackupScheduleSpec spec = new BackupScheduleSpec();
      spec.setStorageConfig(storageConfigName);
      spec.setUniverse(Universe.getOrBadRequest(ybBackupSchedule.getOwnerUUID()).getName());
      spec.setBackupType(BackupScheduleSpec.BackupType.valueOf(params.backupType.toString()));
      spec.setTableByTableBackup(params.tableByTableBackup);
      spec.setKeyspace(params.keyspaceTableList.get(0).keyspace);
      spec.setTimeBeforeDelete(params.timeBeforeDelete);
      if (params.cronExpression != null) {
        spec.setCronExpression(params.cronExpression);
      }
      if (params.schedulingFrequency != 0 && params.frequencyTimeUnit != null) {
        spec.setSchedulingFrequency(
            convertFrequencyToMillis(params.schedulingFrequency, params.frequencyTimeUnit));
      } else if (params.incrementalBackupFrequency != 0
          && params.incrementalBackupFrequencyTimeUnit != null) {
        spec.setIncrementalBackupFrequency(
            convertFrequencyToMillis(
                params.incrementalBackupFrequency, params.incrementalBackupFrequencyTimeUnit));
      }
      spec.setEnablePointInTimeRestore(params.enablePointInTimeRestore);
      backupSchedule.setSpec(spec);
      kubernetesClient
          .resources(BackupSchedule.class)
          .inNamespace(namespace)
          .resource(backupSchedule)
          .create();
    } catch (Exception e) {
      throw new Exception(
          String.format("Unable to create backup schedule: %s type: BackupSchedule", name), e);
    }
  }

  public void createUniverseCr(Universe universe, String providerName, String namespace)
      throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      YBUniverse existing =
          kubernetesClient
              .resources(YBUniverse.class)
              .inNamespace(namespace)
              .withName(universe.getName())
              .get();
      if (existing != null) {
        String existingUuid =
            existing.getMetadata().getAnnotations() != null
                ? existing
                    .getMetadata()
                    .getAnnotations()
                    .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)
                : null;
        if (universe.getUniverseUUID().toString().equals(existingUuid)) {
          log.info(
              "Universe {} already exists with matching UUID, skipping creation",
              universe.getName());
          return;
        } else {
          log.warn(
              "Universe CR {} exists but with different UUID. Existing: {}, Expected: {}",
              universe.getName(),
              existingUuid,
              universe.getUniverseUUID());
          // Still skip to avoid overwriting, but log the mismatch
          return;
        }
      }
      YBUniverse ybUniverse = new YBUniverse();
      ybUniverse.setMetadata(
          new ObjectMetaBuilder()
              .withName(universe.getName())
              .withNamespace(namespace)
              .withFinalizers(YB_FINALIZER)
              .withAnnotations(
                  Map.ofEntries(
                      Map.entry(
                          ResourceAnnotationKeys.YBA_RESOURCE_ID,
                          universe.getUniverseUUID().toString())))
              .build());
      YBUniverseSpec spec = new YBUniverseSpec();

      // Basics
      spec.setUniverseName(universe.getName());
      spec.setNumNodes(
          Long.valueOf(universe.getUniverseDetails().getPrimaryCluster().userIntent.numNodes));
      spec.setReplicationFactor(
          Long.valueOf(
              universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor));
      spec.setEnableNodeToNodeEncrypt(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt);
      spec.setEnableClientToNodeEncrypt(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt);
      spec.setYbSoftwareVersion(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      spec.setProviderName(providerName);
      spec.setEnableIPV6(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableIPV6);
      spec.setEnableLoadBalancer(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableExposingService
              == ExposingServiceState.EXPOSED);
      spec.setPaused(universe.getUniverseDetails().universePaused);

      if (universe.getUniverseDetails().clusters.size() > 1) {
        List<Cluster> readOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
        if (readOnlyClusters == null || readOnlyClusters.isEmpty()) {
          log.warn(
              "Universe {} has multiple clusters but no read-only clusters found",
              universe.getName());
        } else {
          if (readOnlyClusters.size() > 1) {
            log.warn(
                "Universe {} has {} read replica clusters, only importing the first one",
                universe.getName(),
                readOnlyClusters.size());
          }
          Cluster firstReadReplica = readOnlyClusters.get(0);
          ReadReplica rr = new ReadReplica();
          rr.setNumNodes(Long.valueOf(firstReadReplica.userIntent.numNodes));
          rr.setReplicationFactor(Long.valueOf(firstReadReplica.userIntent.replicationFactor));
          universeImporter.setReadReplicaDeviceInfo(rr, firstReadReplica);
          universeImporter.setReadReplicaResourceSpecFromUniverse(rr, firstReadReplica);
          universeImporter.setReadReplicaPlacementInfo(rr, firstReadReplica);
          spec.setReadReplica(rr);
        }
      }

      // Set languages
      universeImporter.setYcqlSpec(
          spec,
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYCQL,
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYCQLAuth);
      universeImporter.setYsqlSpec(
          spec,
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL,
          universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQLAuth);

      // Gflags
      universeImporter.setGflagsSpecFromUniverse(spec, universe);

      // Device info
      universeImporter.setDeviceInfoSpecFromUniverse(spec, universe);
      universeImporter.setTserverResourceSpecFromUniverse(spec, universe);
      universeImporter.setMasterResourceSpecFromUniverse(spec, universe);
      universeImporter.setMasterDeviceInfoSpecFromUniverse(spec, universe);

      // Ybc throttle parameters
      universeImporter.setYbcThrottleParametersSpecFromUniverse(spec, universe);

      // Kubernetes overrides
      universeImporter.setKubernetesOverridesSpecFromUniverse(spec, universe);

      ybUniverse.setSpec(spec);
      YBUniverseStatus status = new YBUniverseStatus();
      status.setCqlEndpoints(getYCQLEndpoints(universe));
      status.setSqlEndpoints(getYSQLEndpoints(universe));
      status.setResourceUUID(universe.getUniverseUUID().toString());
      status.setUniverseState(OperatorStatusUpdater.UniverseState.READY.toString());
      status.setActions(new ArrayList<>());
      ybUniverse.setStatus(status);
      kubernetesClient
          .resources(YBUniverse.class)
          .inNamespace(namespace)
          .resource(ybUniverse)
          .create();
    }
  }

  private List<String> getYCQLEndpoints(Universe universe) {
    List<String> endpoints = new ArrayList<>();
    endpoints.addAll(Arrays.asList(universe.getYQLServerAddresses().split(",")));
    try {
      String cqlServiceEndpoints =
          kubernetesManagerFactory
              .getManager()
              .getKubernetesServiceIPPort(ServerType.YQLSERVER, universe);
      if (cqlServiceEndpoints != null) {
        endpoints.addAll(Arrays.asList(cqlServiceEndpoints.split(",")));
      }
    } catch (Exception e) {
      log.warn("Unable to get YCQL service endpoints", e);
    }
    return endpoints;
  }

  private List<String> getYSQLEndpoints(Universe universe) {
    List<String> endpoints = new ArrayList<>();
    endpoints.addAll(Arrays.asList(universe.getYSQLServerAddresses().split(",")));
    try {
      String sqlServiceEndpoints =
          kubernetesManagerFactory
              .getManager()
              .getKubernetesServiceIPPort(ServerType.YSQLSERVER, universe);
      if (sqlServiceEndpoints != null) {
        endpoints.addAll(Arrays.asList(sqlServiceEndpoints.split(",")));
      }
    } catch (Exception e) {
      log.warn("Unable to get YSQL service endpoints", e);
    }
    return endpoints;
  }

  public CreatePitrConfigParams getCreatePitrConfigParamsFromCr(PitrConfig pitrConfig)
      throws Exception {

    ObjectNode crParams = objectMapper.valueToTree(pitrConfig.getSpec());
    Customer cust = getOperatorCustomer();
    String crUniverseName = crParams.get("universe").asText();
    Universe universe = getUniverseFromNameAndNamespace(cust.getId(), crUniverseName, namespace);
    if (universe == null) {
      throw new Exception("No universe found with name " + crUniverseName);
    }
    UUID universeUUID = universe.getUniverseUUID();
    UUID customerUUID = cust.getUuid();
    String keyspaceName = crParams.get("database").asText();
    crParams.put("universeUUID", universeUUID.toString());
    crParams.put("customerUUID", customerUUID.toString());

    CreatePitrConfigParams createPitrConfigParams =
        validatingFormFactory.getFormDataOrBadRequest(crParams, CreatePitrConfigParams.class);
    createPitrConfigParams.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(pitrConfig));
    return createPitrConfigParams;
  }

  public UpdatePitrConfigParams getUpdatePitrConfigParamsFromCr(PitrConfig pitrConfig)
      throws Exception {

    ObjectNode crParams = objectMapper.valueToTree(pitrConfig.getSpec());
    Customer cust = getOperatorCustomer();
    String crUniverseName = crParams.get("universe").asText();
    Universe universe = getUniverseFromNameAndNamespace(cust.getId(), crUniverseName, namespace);
    if (universe == null) {
      throw new Exception("No universe found with name " + crUniverseName);
    }
    UUID universeUUID = universe.getUniverseUUID();
    UUID customerUUID = cust.getUuid();
    UUID pitrConfigUUID = UUID.fromString(pitrConfig.getStatus().getResourceUUID());
    crParams.put("universeUUID", universeUUID.toString());
    crParams.put("customerUUID", customerUUID.toString());
    crParams.put("pitrConfigUUID", pitrConfigUUID.toString());

    UpdatePitrConfigParams updatePitrConfigParams =
        validatingFormFactory.getFormDataOrBadRequest(crParams, UpdatePitrConfigParams.class);

    updatePitrConfigParams.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(pitrConfig));

    return updatePitrConfigParams;
  }
}
