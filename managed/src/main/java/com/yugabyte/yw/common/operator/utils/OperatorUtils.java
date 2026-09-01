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
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.AwsKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil.AzuKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.CiphertrustEARServiceUtil.CipherTrustKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil.GcpKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
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
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigReplaceReplicaForm;
import com.yugabyte.yw.forms.DrConfigRestartForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.ProxyConfigUpdateParams;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse.ThrottleParamValue;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
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
import com.yugabyte.yw.models.helpers.exporters.UniverseExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.SimpleServerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.TServerLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import io.fabric8.kubernetes.api.model.HasMetadata;
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
import io.fabric8.kubernetes.client.KubernetesClientException;
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
import io.yugabyte.operator.v1alpha1.KMSConfig;
import io.yugabyte.operator.v1alpha1.KMSConfigSpec;
import io.yugabyte.operator.v1alpha1.KMSConfigStatus;
import io.yugabyte.operator.v1alpha1.PitrConfig;
import io.yugabyte.operator.v1alpha1.PitrRestore;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.ReleaseSpec;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.StorageConfigSpec;
import io.yugabyte.operator.v1alpha1.StorageConfigStatus;
import io.yugabyte.operator.v1alpha1.TelemetryProvider;
import io.yugabyte.operator.v1alpha1.TelemetryProviderStatus;
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
import io.yugabyte.operator.v1alpha1.ybuniversespec.EncryptionAtRest;
import io.yugabyte.operator.v1alpha1.ybuniversespec.ReadReplica;
import io.yugabyte.operator.v1alpha1.ybuniversespec.Telemetry;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YbcThrottleParameters;
import java.nio.file.Paths;
import java.time.OffsetDateTime;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;
import org.yb.client.YBClientApi;
import play.libs.Json;

@Slf4j
public class OperatorUtils {

  public static final String IGNORE_RECONCILER_ADD_LABEL = "ignore-reconciler-add";
  public static final String YB_FINALIZER = "finalizer.k8soperator.yugabyte.com";
  public static final String AUTO_PROVIDER_LABEL = "auto-provider";
  public static final int KUBERNETES_NAME_MAX_LENGTH = 63;
  public static final String PROVIDER_KUBECONFIG_KEY = "PROVIDER_KUBECONFIG";

  /**
   * Used to deep-copy telemetry config sections before neutralizing their server-derived fields for
   * comparison. Plain mapper: the internal telemetry configs are camelCase POJOs whose computed
   * properties are {@code @JsonIgnore}, so a round trip through it preserves the stored state
   * exactly.
   */
  private static final ObjectMapper TELEMETRY_COPY_MAPPER = new ObjectMapper();

  /**
   * KMS providers the operator implements, both for reconciling a KMSConfig CR and for importing an
   * existing YBA config into one. Other providers surface as an Error state on the CR, and block a
   * universe that uses them from being imported.
   */
  public static final Set<KeyProvider> SUPPORTED_KMS_PROVIDERS =
      Collections.unmodifiableSet(
          EnumSet.of(
              KeyProvider.HASHICORP,
              KeyProvider.AWS,
              KeyProvider.GCP,
              KeyProvider.AZU,
              KeyProvider.CIPHERTRUST,
              KeyProvider.OCI));

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

  public synchronized Config getK8sClientConfig() {
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

  /**
   * Returns the UUID of the local PlatformInstance when HA is configured, or empty if HA is not set
   * up. Used to track which YBA instances have applied a resource to their K8s cluster.
   */
  public Optional<UUID> getLocalPlatformInstanceUuid() {
    return HighAvailabilityConfig.get()
        .flatMap(HighAvailabilityConfig::getLocal)
        .map(PlatformInstance::getUuid);
  }

  public Universe getUniverseFromNameAndNamespace(
      Long customerId, String universeName, String namespace) throws Exception {
    KubernetesResourceDetails ybUniverseResourceDetails = new KubernetesResourceDetails();
    ybUniverseResourceDetails.name = universeName;
    ybUniverseResourceDetails.namespace = namespace;
    YBUniverse ybUniverse = getYBUniverse(ybUniverseResourceDetails);
    if (ybUniverse == null) {
      log.debug("YBUniverse '{}' not found in namespace '{}'", universeName, namespace);
      return null;
    }
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
   * Resolves a KMSConfig CR (by name, in the given namespace) to its YBA config UUID. Throws when
   * the config CR is missing, not yet Ready/InUse, or has no resolved config UUID, so the caller
   * can surface a clear error rather than proceeding without a valid KMS config.
   *
   * @param kmsConfigCrName the KMSConfig CR name
   * @param namespace the namespace to look it up in
   * @return the resolved YBA KMS config UUID
   */
  public UUID resolveReadyKmsConfigUuid(String kmsConfigCrName, String namespace) throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      KMSConfig kmsConfigCr =
          kubernetesClient
              .resources(KMSConfig.class)
              .inNamespace(namespace)
              .withName(kmsConfigCrName)
              .get();
      if (kmsConfigCr == null) {
        throw new Exception("KMS config CR '" + kmsConfigCrName + "' not found");
      }
      KMSConfigStatus status = kmsConfigCr.getStatus();
      String state = status == null ? null : status.getState();
      String resourceUUID = status == null ? null : status.getResourceUUID();
      // Ready or InUse both mean the config exists in YBA with a valid UUID.
      if (!"Ready".equals(state) && !"InUse".equals(state)) {
        throw new Exception(
            "KMS config CR '" + kmsConfigCrName + "' is not ready (state: " + state + ")");
      }
      if (StringUtils.isBlank(resourceUUID)) {
        throw new Exception(
            "KMS config CR '" + kmsConfigCrName + "' has no resolved config UUID yet");
      }
      return UUID.fromString(resourceUUID);
    }
  }

  /**
   * Resolves a TelemetryProvider CR (by name, in the given namespace) to its YBA telemetry provider
   * UUID. Mirrors {@link #resolveReadyKmsConfigUuid}: throws when the CR is missing, has not
   * reached a state that means the provider exists in YBA, or has not published a resolved UUID
   * yet, so a universe CR never gets an exporter pointing at a provider YBA does not have.
   *
   * @param telemetryProviderCrName the TelemetryProvider CR name
   * @param namespace the namespace to look it up in
   * @return the resolved YBA telemetry provider UUID
   */
  public UUID resolveReadyTelemetryProviderUuid(String telemetryProviderCrName, String namespace)
      throws Exception {
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      TelemetryProvider telemetryProviderCr =
          kubernetesClient
              .resources(TelemetryProvider.class)
              .inNamespace(namespace)
              .withName(telemetryProviderCrName)
              .get();
      if (telemetryProviderCr == null) {
        throw new Exception(
            "Telemetry provider CR '"
                + telemetryProviderCrName
                + "' not found in namespace '"
                + namespace
                + "'");
      }
      TelemetryProviderStatus status = telemetryProviderCr.getStatus();
      String state = status == null ? null : status.getState();
      String resourceUUID = status == null ? null : status.getResourceUUID();
      // Ready or InUse both mean the provider exists in YBA with a valid UUID. InUse is what the
      // TelemetryProvider reconciler reports after refusing to delete a provider a universe still
      // references, which must not stop that universe from reconciling.
      if (!"Ready".equals(state) && !"InUse".equals(state)) {
        throw new Exception(
            "Telemetry provider CR '"
                + telemetryProviderCrName
                + "' is not ready (state: "
                + state
                + ")");
      }
      if (StringUtils.isBlank(resourceUUID)) {
        throw new Exception(
            "Telemetry provider CR '"
                + telemetryProviderCrName
                + "' has no resolved provider UUID yet");
      }
      return UUID.fromString(resourceUUID);
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
   * Adds a YBA resource ID annotation to the given Kubernetes resource metadata. Uses {@code edit}
   * to atomically read the latest resource state from the server and apply the annotation. If the
   * resource already has the annotation, no modification is made.
   *
   * @param resource The Kubernetes resource to annotate
   * @param resourceId The UUID to store as the YBA resource ID annotation
   */
  public static <T extends HasMetadata> void maybeAddYbaResourceId(
      T resource,
      UUID resourceId,
      MixedOperation<T, KubernetesResourceList<T>, Resource<T>> resourceClient) {
    ObjectMeta metadata = resource.getMetadata();
    if (metadata == null) {
      throw new RuntimeException(String.format("Metadata is null for resource: %s", resource));
    }
    Map<String, String> annotations = metadata.getAnnotations();
    if (annotations != null && annotations.containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      return;
    }
    try {
      resourceClient
          .inNamespace(metadata.getNamespace())
          .withName(metadata.getName())
          .edit(
              r -> {
                ObjectMeta serverMeta = r.getMetadata();
                Map<String, String> serverAnnotations = serverMeta.getAnnotations();
                if (serverAnnotations != null
                    && serverAnnotations.containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
                  return r;
                }
                r.setMetadata(
                    new ObjectMetaBuilder(serverMeta)
                        .addToAnnotations(
                            ResourceAnnotationKeys.YBA_RESOURCE_ID, resourceId.toString())
                        .build());
                return r;
              });
    } catch (KubernetesClientException e) {
      log.warn(
          "Failed to add YBA resource ID '{}' annotation to resource: {}", resourceId, resource, e);
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

  public boolean shouldUpdatePrimaryCluster(
      Cluster currentCluster, YBUniverse newYbUniverse, UserIntent incomingIntent) {
    UserIntent currentUserIntent = currentCluster.userIntent;
    int newNumNodes = newYbUniverse.getSpec().getNumNodes().intValue();
    K8SNodeResourceSpec newMasterK8SNodeResourceSpec =
        toNodeResourceSpec(
            newYbUniverse.getSpec().getMasterResourceSpec(), s -> s.getCpu(), s -> s.getMemory());
    K8SNodeResourceSpec newTserverK8SNodeResourceSpec =
        toNodeResourceSpec(
            newYbUniverse.getSpec().getTserverResourceSpec(), s -> s.getCpu(), s -> s.getMemory());
    return !(currentUserIntent.numNodes == newNumNodes)
        || checkIfDeviceInfoChanged(currentCluster, incomingIntent, newYbUniverse)
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

  public boolean shouldUpdateReadReplica(
      Universe universe, YBUniverse ybUniverse, UserIntent incomingIntent) {
    int readReplicaClusterCount = universe.getUniverseDetails().getReadOnlyClusters().size();
    boolean hasReadReplica = ybUniverse.getSpec().getReadReplica() != null;
    // No read replica exists or none requested
    if (readReplicaClusterCount == 0 || !hasReadReplica) {
      return false;
    } else {
      Cluster readReplicaCluster = universe.getUniverseDetails().getReadOnlyClusters().get(0);
      UserIntent readReplicaUserIntent = readReplicaCluster.userIntent;
      PlacementInfo readReplicaPlacementInfo = readReplicaCluster.placementInfo;
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
          || checkIfDeviceInfoChanged(readReplicaCluster, incomingIntent, ybUniverse)
          || !readReplicaUserIntent.tserverK8SNodeResourceSpec.equals(
              newReadReplicaTserverK8SNodeResourceSpec);
    }
  }

  public boolean checkIfDeviceInfoChanged(
      Cluster curCluster, UserIntent newIntent, YBUniverse ybUniverse) {
    if (ybUniverse.getSpec().getTserverVolume() != null
        || ybUniverse.getSpec().getMasterVolume() != null) {
      UserIntent newIntentClone = newIntent.clone();
      AtomicBoolean deviceInfoChanged = new AtomicBoolean(false);
      // If new userIntent does not contain perAZ overrides for tserver, first assign old
      // userIntentOverrides
      if (!(ybUniverse.getSpec().getTserverVolume() != null
          && ybUniverse.getSpec().getTserverVolume().getPerAZ() != null)) {
        newIntentClone.updateAZVolumeOverrides(
            curCluster.userIntent,
            curCluster.placementInfo.getAllAZUUIDs(),
            null,
            false /* isDedicatedMaster */);
      }
      // If new userIntent does not contain perAZ overrides for master, first assign old
      // userIntentOverrides
      if (curCluster.clusterType != ClusterType.ASYNC
          && !(ybUniverse.getSpec().getMasterVolume() != null
              && ybUniverse.getSpec().getMasterVolume().getPerAZ() != null)) {
        newIntentClone.updateAZVolumeOverrides(
            curCluster.userIntent,
            curCluster.placementInfo.getAllAZUUIDs(),
            null,
            true /* isDedicatedMaster */);
      }
      curCluster
          .placementInfo
          .getAllAZUUIDs()
          .forEach(
              azUUID -> {
                DeviceInfo tsDeviceInfo =
                    curCluster.userIntent.getDeviceInfoForAz(azUUID, ServerType.TSERVER);
                DeviceInfo newTsDeviceInfo =
                    newIntentClone.getDeviceInfoForAz(azUUID, ServerType.TSERVER);
                log.debug(
                    "Comparing tserver device info for AZ {}: old {}, new {}",
                    azUUID,
                    Json.toJson(tsDeviceInfo),
                    Json.toJson(newTsDeviceInfo));
                deviceInfoChanged.set(
                    deviceInfoChanged.get() || !tsDeviceInfo.equals(newTsDeviceInfo));

                if (curCluster.clusterType != ClusterType.ASYNC) {
                  DeviceInfo masterDeviceInfo =
                      curCluster.userIntent.getDeviceInfoForAz(azUUID, ServerType.MASTER);
                  DeviceInfo newMasterDeviceInfo =
                      newIntentClone.getDeviceInfoForAz(azUUID, ServerType.MASTER);
                  log.debug(
                      "Comparing master device info for AZ {}: old {}, new {}",
                      azUUID,
                      Json.toJson(masterDeviceInfo),
                      Json.toJson(newMasterDeviceInfo));
                  deviceInfoChanged.set(
                      deviceInfoChanged.get() || !masterDeviceInfo.equals(newMasterDeviceInfo));
                }
              });
      log.debug("Device info changed: {}", deviceInfoChanged.get());
      return deviceInfoChanged.get();
    } else {
      // volumeSize is an Integer: compare by value, not by reference.
      boolean tserverSizeChanged =
          !Objects.equals(
              curCluster.userIntent.deviceInfo.volumeSize, newIntent.deviceInfo.volumeSize);
      boolean masterSizeChanged = false;
      if (curCluster.clusterType != ClusterType.ASYNC) {
        masterSizeChanged =
            !Objects.equals(
                curCluster.userIntent.masterDeviceInfo.volumeSize,
                newIntent.masterDeviceInfo.volumeSize);
      }
      return tserverSizeChanged || masterSizeChanged;
    }
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

  /**
   * Maps tserverVolume from CRD to DeviceInfo object. This is the new way to specify volume
   * configuration, replacing deviceInfo.
   *
   * @param tserverVolume TserverVolume object from CRD spec
   * @return DeviceInfo object or null if tserverVolume is null
   */
  public DeviceInfo mapTserverVolume(
      io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume) {
    if (tserverVolume == null) {
      return null;
    }

    DeviceInfo di = new DeviceInfo();

    Long numVols = tserverVolume.getNumVolumes();
    if (numVols != null) {
      di.numVolumes = numVols.intValue();
    }

    Long volSize = tserverVolume.getVolumeSize();
    if (volSize != null) {
      di.volumeSize = volSize.intValue();
    }

    di.storageClass = tserverVolume.getStorageClass();

    return di;
  }

  /**
   * Maps masterVolume from CRD to DeviceInfo object. This is the new way to specify volume
   * configuration, replacing masterDeviceInfo.
   *
   * @param masterVolume MasterVolume object from CRD spec
   * @return DeviceInfo object or null if masterVolume is null
   */
  public DeviceInfo mapMasterVolume(
      io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume masterVolume) {
    if (masterVolume == null) {
      return null;
    }

    DeviceInfo di = new DeviceInfo();

    Long numVols = masterVolume.getNumVolumes();
    if (numVols != null) {
      di.numVolumes = numVols.intValue();
    }

    Long volSize = masterVolume.getVolumeSize();
    if (volSize != null) {
      di.volumeSize = volSize.intValue();
    }

    di.storageClass = masterVolume.getStorageClass();

    return di;
  }

  /**
   * Maps read replica tserverVolume from CRD to DeviceInfo object. This is the new way to specify
   * volume configuration for read replicas, replacing deviceInfo.
   *
   * @param tserverVolume TserverVolume object from read replica CRD spec
   * @return DeviceInfo object or null if tserverVolume is null
   */
  public DeviceInfo mapReadReplicaTserverVolume(
      io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverVolume tserverVolume) {
    if (tserverVolume == null) {
      return null;
    }

    DeviceInfo di = new DeviceInfo();

    Long numVols = tserverVolume.getNumVolumes();
    if (numVols != null) {
      di.numVolumes = numVols.intValue();
    }

    Long volSize = tserverVolume.getVolumeSize();
    if (volSize != null) {
      di.volumeSize = volSize.intValue();
    }

    di.storageClass = tserverVolume.getStorageClass();

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

  public DeviceInfo defaultDeviceInfo() {
    DeviceInfo masterDeviceInfo = new DeviceInfo();
    masterDeviceInfo.volumeSize = 100;
    masterDeviceInfo.numVolumes = 2;
    return masterDeviceInfo;
  }

  public static DeviceInfo defaultMasterDeviceInfo() {
    DeviceInfo masterDeviceInfo = new DeviceInfo();
    masterDeviceInfo.volumeSize = 50;
    masterDeviceInfo.numVolumes = 1;
    return masterDeviceInfo;
  }

  public boolean universeAndSpecMismatch(
      Customer cust,
      Universe u,
      YBUniverse ybUniverse,
      UserIntent newPrimaryIntent,
      UserIntent newReadReplicaIntent) {
    return universeAndSpecMismatch(
        cust, u, ybUniverse, newPrimaryIntent, newReadReplicaIntent, null);
  }

  public boolean universeAndSpecMismatch(
      Customer cust,
      Universe u,
      YBUniverse ybUniverse,
      UserIntent newPrimaryIntent,
      UserIntent newReadReplicaIntent,
      @Nullable TaskInfo prevTaskToRerun) {
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

    Provider provider = Util.getSingleProvider(currentUserIntent);
    // Get all required params
    SpecificGFlags specGFlags = getGFlagsFromSpec(ybUniverse, provider);
    String incomingOverrides =
        getKubernetesOverridesString(ybUniverse.getSpec().getKubernetesOverrides());
    String incomingYbSoftwareVersion = ybUniverse.getSpec().getYbSoftwareVersion();
    // Use new volume fields if any are present, otherwise fall back to old deviceInfo fields
    // If tserverVolume or masterVolume is present, use new fields for both (mutual exclusivity)
    DeviceInfo incomingDeviceInfo;
    DeviceInfo incomingMasterDeviceInfo;
    if (ybUniverse.getSpec().getTserverVolume() != null
        || ybUniverse.getSpec().getMasterVolume() != null) {
      // Use new volume fields
      incomingDeviceInfo = mapTserverVolume(ybUniverse.getSpec().getTserverVolume());
      incomingMasterDeviceInfo = mapMasterVolume(ybUniverse.getSpec().getMasterVolume());
    } else {
      // Use old deviceInfo fields
      incomingDeviceInfo = mapDeviceInfo(ybUniverse.getSpec().getDeviceInfo());
      incomingMasterDeviceInfo = mapMasterDeviceInfo(ybUniverse.getSpec().getMasterDeviceInfo());
    }
    int incomingNumNodes = (int) ybUniverse.getSpec().getNumNodes().longValue();
    Boolean pauseChangeRequired =
        ybUniverse.getSpec().getPaused() != u.getUniverseDetails().universePaused;

    if (prevTaskToRerun != null) {
      TaskType specificTaskTypeToRerun = prevTaskToRerun.getTaskType();
      switch (specificTaskTypeToRerun) {
        case EditKubernetesUniverse:
          UniverseDefinitionTaskParams prevTaskParams =
              Json.fromJson(prevTaskToRerun.getTaskParams(), UniverseDefinitionTaskParams.class);
          return shouldUpdatePrimaryCluster(
                  u.getUniverseDetails().getPrimaryCluster(), ybUniverse, newPrimaryIntent)
              || shouldUpdateReadReplica(u, ybUniverse, newReadReplicaIntent);
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
        case UpdateProxyConfig:
          ProxyConfigUpdateParams proxyConfigParams =
              Json.fromJson(prevTaskToRerun.getTaskParams(), ProxyConfigUpdateParams.class);
          return !Objects.equals(
              proxyConfigParams.getPrimaryCluster().userIntent.getProxyConfig(),
              newPrimaryIntent.getProxyConfig());
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
            || shouldUpdatePrimaryCluster(
                u.getUniverseDetails().getPrimaryCluster(), ybUniverse, newPrimaryIntent);
    log.trace("primary cluster mismatch: {}", mismatch);
    mismatch =
        mismatch
            || !Objects.equals(
                currentUserIntent.getProxyConfig(), newPrimaryIntent.getProxyConfig());
    log.trace("proxy config mismatch: {}", mismatch);
    mismatch =
        mismatch || shouldAddReadReplica(u, ybUniverse) || shouldRemoveReadReplica(u, ybUniverse);
    log.trace("read replica count mismatch: {}", mismatch);
    mismatch = mismatch || shouldUpdateReadReplica(u, ybUniverse, newReadReplicaIntent);
    log.trace("read replica mismatch: {}", mismatch);
    mismatch =
        mismatch
            || !StringUtils.equals(currentUserIntent.ybSoftwareVersion, incomingYbSoftwareVersion);
    log.trace("version mismatch: {}", mismatch);
    mismatch = mismatch || pauseChangeRequired;
    log.trace("pause mismatch: {}", mismatch);
    mismatch = mismatch || isThrottleParamUpdate(u, ybUniverse);
    log.trace("throttle mismatch: {}", mismatch);
    mismatch =
        mismatch
            || !(u.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()
                == ybUniverse.getSpec().getUseYbdbInbuiltYbc());
    log.trace("Toggle Immutable YBC mismatch: {}", mismatch);
    mismatch = mismatch || shouldRotateCerts(u, ybUniverse, cust.getUuid());
    log.trace("certificate mismatch: {}", mismatch);
    mismatch = mismatch || shouldToggleTls(currentUserIntent, ybUniverse);
    log.trace("tls parameters mismatch: {}", mismatch);
    mismatch = mismatch || shouldUpdateEncryptionAtRest(u, ybUniverse);
    log.trace("encryption at rest mismatch: {}", mismatch);
    mismatch = mismatch || shouldUpdateTelemetry(u, ybUniverse);
    log.trace("telemetry mismatch: {}", mismatch);
    return mismatch;
  }

  /** Whether encryption at rest is on in the spec. Defaults to true when unset, as does the CRD. */
  public static boolean earEnabled(EncryptionAtRest ear) {
    return ear.getEnabled() == null || ear.getEnabled();
  }

  /** What the universe CR's encryptionAtRest block asks for, relative to the live universe. */
  public enum EarChangeType {
    /** The universe is already in the requested state. */
    NONE,
    /** Turn encryption at rest on, or point it at a different KMS config. */
    ENABLE,
    /** Turn encryption at rest off. */
    DISABLE,
    /** EAR is requested, but the KMS config CR it names is missing or not Ready yet. */
    KMS_CONFIG_NOT_READY
  }

  /** The outcome of {@link #getEncryptionAtRestChange}. */
  @Getter
  @AllArgsConstructor
  public static class EarChange {
    private final EarChangeType type;

    /** The KMS config the spec names, null when it names none or it could not be resolved. */
    private final UUID desiredConfigUUID;

    /** The KMS config the universe's active key is held under, null when there is none. */
    private final UUID currentConfigUUID;

    /** Why the KMS config could not be resolved. Null unless the type is KMS_CONFIG_NOT_READY. */
    private final String kmsConfigError;

    /** Whether this change needs a task submitted for it. */
    public boolean isActionable() {
      return type == EarChangeType.ENABLE || type == EarChangeType.DISABLE;
    }

    /**
     * Whether applying this change re-encrypts the universe key under a different master key. True
     * for a rotation between two KMS configs, and also when re-enabling EAR under a config other
     * than the one the universe's still-active key was left under.
     */
    public boolean rotatesMasterKey() {
      return desiredConfigUUID != null
          && currentConfigUUID != null
          && !desiredConfigUUID.equals(currentConfigUUID);
    }
  }

  /**
   * Works out the encryption at rest change a universe CR is asking for: EAR off to on, on to off,
   * a different KMS config (master key rotation), or nothing.
   *
   * <p>This is the one place that decision is made. Two callers act on it: shouldUpdateUniverse
   * uses it to decide whether an edit is queued at all (a spec change touching only
   * encryptionAtRest is otherwise treated as a No-Op and dropped), and
   * YBUniverseReconciler.handleEncryptionAtRestChange uses it to build and submit the task. They
   * have to agree - queueing an edit the handler then declines to act on requeues the resource
   * forever - which is why they read the same answer instead of each deriving their own.
   */
  public EarChange getEncryptionAtRestChange(Universe u, YBUniverse ybUniverse) {
    EncryptionAtRest ear = ybUniverse.getSpec().getEncryptionAtRest();
    if (ear == null) {
      // No EAR block: leave the universe's EAR state unchanged (disable is via enabled=false).
      return new EarChange(EarChangeType.NONE, null, null, null);
    }
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    boolean currentlyEnabled =
        details.encryptionAtRestConfig != null
            && details.encryptionAtRestConfig.encryptionAtRestEnabled;
    KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(u.getUniverseUUID());
    UUID currentConfigUUID = activeKey == null ? null : activeKey.getConfigUuid();
    boolean desiredEnabled = StringUtils.isNotBlank(ear.getKmsConfig()) && earEnabled(ear);

    if (!desiredEnabled) {
      return new EarChange(
          currentlyEnabled ? EarChangeType.DISABLE : EarChangeType.NONE,
          null,
          currentConfigUUID,
          null);
    }

    UUID desiredConfigUUID;
    try {
      desiredConfigUUID =
          resolveReadyKmsConfigUuid(ear.getKmsConfig(), ybUniverse.getMetadata().getNamespace());
    } catch (Exception e) {
      // The KMS config CR is missing or not Ready yet. The caller decides what to do about it:
      // queueing an edit that cannot complete would only flip the universe to ERROR_UPDATING on
      // every resync and block unrelated edits. Once the KMS config goes Ready, a resync picks
      // this up.
      log.warn(
          "Encryption at rest change for universe {} needs KMS config '{}', which is unusable: {}",
          u.getName(),
          ear.getKmsConfig(),
          e.getMessage());
      return new EarChange(
          EarChangeType.KMS_CONFIG_NOT_READY, null, currentConfigUUID, e.getMessage());
    }
    if (currentlyEnabled && desiredConfigUUID.equals(currentConfigUUID)) {
      return new EarChange(EarChangeType.NONE, desiredConfigUUID, currentConfigUUID, null);
    }
    return new EarChange(EarChangeType.ENABLE, desiredConfigUUID, currentConfigUUID, null);
  }

  /** Whether the universe CR's encryptionAtRest block calls for an edit to the universe. */
  public boolean shouldUpdateEncryptionAtRest(Universe u, YBUniverse ybUniverse) {
    return getEncryptionAtRestChange(u, ybUniverse).isActionable();
  }

  /*--- Telemetry export helper methods ---*/

  /**
   * The telemetry export config the universe CR asks for, with every exporter's TelemetryProvider
   * CR name resolved to its YBA UUID.
   *
   * @throws Exception if an exporter names a TelemetryProvider CR that is missing, not ready, or
   *     has not published its resolved UUID yet
   */
  public TelemetryConfig getDesiredTelemetryConfig(YBUniverse ybUniverse) throws Exception {
    String namespace =
        ybUniverse.getMetadata() == null ? null : ybUniverse.getMetadata().getNamespace();
    Telemetry telemetry = ybUniverse.getSpec() == null ? null : ybUniverse.getSpec().getTelemetry();
    return UniverseTelemetrySpecConverter.toTelemetryConfig(
        telemetry, crName -> resolveReadyTelemetryProviderUuid(crName, namespace));
  }

  /**
   * Whether the universe CR's telemetry block differs from what the universe currently has applied,
   * and therefore whether an export-telemetry task should be submitted.
   */
  public boolean shouldUpdateTelemetry(Universe universe, YBUniverse ybUniverse) {
    TelemetryConfig desired;
    try {
      desired = getDesiredTelemetryConfig(ybUniverse);
    } catch (Exception e) {
      // An exporter names a TelemetryProvider CR that is missing or not ready yet. Queueing an edit
      // that cannot complete would only flip the universe to ERROR_UPDATING on every resync and
      // block unrelated edits.
      log.warn(
          "Cannot determine the desired telemetry config for universe {}: {}",
          universe.getName(),
          e.getMessage());
      return false;
    }
    TelemetryConfig current = OtelCollectorUtil.getCurrentTelemetryConfig(universe);
    for (ExportType type : ExportType.values()) {
      if (telemetrySectionDiffers(type, desired, current)) {
        log.debug(
            "Telemetry section {} of universe {} differs from the CR spec",
            type,
            universe.getName());
        return true;
      }
    }
    return false;
  }

  /**
   * Whether one export type's section differs between the desired and current configs, comparing
   * authored fields only.
   *
   * <p>an "authored" field is one that is explicitly set, not derived. For example, exportActive in
   * AuditLogConfig.
   */
  @VisibleForTesting
  public static boolean telemetrySectionDiffers(
      ExportType type,
      @Nullable TelemetryConfig desiredConfig,
      @Nullable TelemetryConfig currentConfig) {
    // A null aggregate means "nothing configured", same as an aggregate with all-null sections.
    TelemetryConfig desired = desiredConfig != null ? desiredConfig : new TelemetryConfig();
    TelemetryConfig current = currentConfig != null ? currentConfig : new TelemetryConfig();
    switch (type) {
      case AUDIT_LOGS:
        return !Objects.equals(
            authoredView(desired.getAuditLogConfig()), authoredView(current.getAuditLogConfig()));
      case QUERY_LOGS:
        return !Objects.equals(
            authoredView(desired.getQueryLogConfig()), authoredView(current.getQueryLogConfig()));
      case METRICS:
        return !Objects.equals(
            authoredView(desired.getMetricsExportConfig()),
            authoredView(current.getMetricsExportConfig()));
      case MASTER_LOGS:
        return !Objects.equals(
            authoredView(desired.getMasterLogConfig()), authoredView(current.getMasterLogConfig()));
      case TSERVER_LOGS:
        return !Objects.equals(
            authoredView(desired.getTserverLogConfig()),
            authoredView(current.getTserverLogConfig()));
      case YSQL_CONN_MGR_LOGS:
        return !Objects.equals(
            authoredView(desired.getYsqlConnMgrLogConfig()),
            authoredView(current.getYsqlConnMgrLogConfig()));
      case CONTROLLER_LOGS:
        return !Objects.equals(
            authoredView(desired.getControllerLogConfig()),
            authoredView(current.getControllerLogConfig()));
      case NODE_AGENT_LOGS:
      case YNP_LOGS:
        // VM-only (ExportType.isSupportedOnKubernetes() is false), so they are absent from the
        // universe CRD entirely and the operator never reconciles them.
        return false;
      default:
        throw new IllegalArgumentException(
            "Unhandled export type in the operator telemetry comparison: " + type);
    }
  }

  /**
   * Audit Log Config with derived fields forced to false. - exportActive - ysqlAuditConfig.enabled
   * - ycqlAuditConfig.enabled
   */
  private static AuditLogConfig authoredView(AuditLogConfig config) {
    if (config == null) {
      return null;
    }
    AuditLogConfig view = authoredCopy(config);
    view.setExportActive(false);
    if (config.getYsqlAuditConfig() != null && config.getYsqlAuditConfig().isEnabled()) {
      view.getYsqlAuditConfig().setEnabled(false);
    } else {
      view.setYsqlAuditConfig(null);
    }
    if (config.getYcqlAuditConfig() != null && config.getYcqlAuditConfig().isEnabled()) {
      view.getYcqlAuditConfig().setEnabled(false);
    } else {
      view.setYcqlAuditConfig(null);
    }
    view.setUniverseLogsExporterConfig(authoredExporters(view.getUniverseLogsExporterConfig()));
    return view;
  }

  /**
   * QueryLogConfig with derived fields forced to false. - exportActive - ysqlQueryLogConfig.enabled
   */
  private static QueryLogConfig authoredView(QueryLogConfig config) {
    if (config == null) {
      return null;
    }
    QueryLogConfig view = authoredCopy(config);
    view.setExportActive(false);
    if (config.getYsqlQueryLogConfig() != null && config.getYsqlQueryLogConfig().isEnabled()) {
      view.getYsqlQueryLogConfig().setEnabled(false);
    } else {
      view.setYsqlQueryLogConfig(null);
    }
    view.setUniverseLogsExporterConfig(authoredExporters(view.getUniverseLogsExporterConfig()));
    return view;
  }

  /** MetricsExportConfig has no derived fields, just handle exporter configs. */
  private static MetricsExportConfig authoredView(MetricsExportConfig config) {
    if (config == null) {
      return null;
    }
    MetricsExportConfig view = authoredCopy(config);
    view.setUniverseMetricsExporterConfig(
        authoredExporters(view.getUniverseMetricsExporterConfig()));
    return view;
  }

  /** MasterLogConfig has no derived fields, just handle exporter configs. */
  private static MasterLogConfig authoredView(MasterLogConfig config) {
    if (config == null) {
      return null;
    }
    MasterLogConfig view = authoredCopy(config);
    view.setUniverseLogsExporterConfig(authoredExporters(view.getUniverseLogsExporterConfig()));
    return view;
  }

  /** TserverLogConfig has no derived fields, just handle exporter configs. */
  private static TServerLogConfig authoredView(TServerLogConfig config) {
    if (config == null) {
      return null;
    }
    TServerLogConfig view = authoredCopy(config);
    view.setUniverseLogsExporterConfig(authoredExporters(view.getUniverseLogsExporterConfig()));
    return view;
  }

  /** SimpleServerLogConfig has no derived fields, just handle exporter configs. */
  private static <T extends SimpleServerLogConfig> T authoredView(T config) {
    if (config == null) {
      return null;
    }
    T view = authoredCopy(config);
    view.setUniverseLogsExporterConfig(authoredExporters(view.getUniverseLogsExporterConfig()));
    return view;
  }

  /** Exporters normalized - nulls become empty maps or strings */
  private static <T extends UniverseExporterConfig> List<T> authoredExporters(List<T> exporters) {
    if (CollectionUtils.isEmpty(exporters)) {
      return Collections.emptyList();
    }
    for (T exporter : exporters) {
      if (exporter.getAdditionalTags() == null) {
        exporter.setAdditionalTags(new HashMap<>());
      }
      if (exporter instanceof UniverseMetricsExporterConfig) {
        UniverseMetricsExporterConfig metricsExporter = (UniverseMetricsExporterConfig) exporter;
        if (metricsExporter.getMetricsPrefix() == null) {
          metricsExporter.setMetricsPrefix("");
        }
      }
    }
    return exporters;
  }

  /**
   * Deep copy of a telemetry section, so the authored views can neutralize derived fields without
   * mutating either the desired config or - importantly - the live objects the current config was
   * read from (they belong to the universe details and the export_telemetry_config row).
   */
  @SuppressWarnings("unchecked")
  private static <T> T authoredCopy(T config) {
    return (T) TELEMETRY_COPY_MAPPER.convertValue(config, config.getClass());
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
      KubernetesResourceDetails owner,
      UUID localInstanceUuid) {
    Secret secret = getSecret(name, namespace);
    if (secret == null) {
      log.warn("Secret {} not found", name);
      return null;
    }
    resourceTracker.trackDependency(owner, secret, localInstanceUuid);
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
    // A secret carries data, stringData, or neither - whichever is absent comes back null.
    if (secret.getData() != null && secret.getData().get(key) != null) {
      return new String(Base64.getDecoder().decode(secret.getData().get(key)));
    }
    return secret.getStringData() != null ? secret.getStringData().get(key) : null;
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
    // Paused universes have no reachable YBC endpoints; probing them stalls reconcile.
    if (universe.getUniverseDetails().universePaused) {
      return false;
    }
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

  /*--- Certificate rotation helper methods ---*/

  /**
   * Checks if certificate rotation is needed for the universe.
   *
   * @param universe the current universe
   * @param ybUniverse the YBUniverse spec
   * @param customerUUID the customer UUID
   * @return true if certificate rotation is needed, false otherwise
   */
  public boolean shouldRotateCerts(Universe universe, YBUniverse ybUniverse, UUID customerUUID) {
    String specRootCAName = ybUniverse.getSpec().getRootCA();
    UUID currentRootCA = universe.getUniverseDetails().rootCA;

    // If no cert specified in spec, no rotation needed
    if (StringUtils.isBlank(specRootCAName)) {
      return false;
    }

    CertificateInfo specRootCACert = CertificateInfo.get(customerUUID, specRootCAName);
    if (specRootCACert == null) {
      log.warn("Certificate {} not found for customer {}", specRootCAName, customerUUID);
      return false;
    }

    // Check if the certificate UUID differs from the current one
    return !specRootCACert.getUuid().equals(currentRootCA);
  }

  /*--- TLS toggle helper methods ---*/

  /**
   * Checks if the encryption-in-transit settings in the spec differ from the universe, requiring a
   * TLS toggle operation.
   *
   * @param currentUserIntent the current primary cluster user intent
   * @param ybUniverse the YBUniverse spec
   * @return true if node-to-node or client-to-node encryption settings have changed
   */
  public boolean shouldToggleTls(UserIntent currentUserIntent, YBUniverse ybUniverse) {
    return currentUserIntent.enableNodeToNodeEncrypt
            != ybUniverse.getSpec().getEnableNodeToNodeEncrypt()
        || currentUserIntent.enableClientToNodeEncrypt
            != ybUniverse.getSpec().getEnableClientToNodeEncrypt();
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
    crSpec.setUseTablespaces(params.useTablespaces);
    crSpec.setUseRoles(params.getUseRoles());
    crSpec.setUsePrivileges(params.getUsePrivileges());
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
    YBClientApi client = ybService.getUniverseClient(sourceUniverse);
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

  public DrConfigSetDatabasesForm getDrConfigSetDatabasesFormFromCr(DrConfig drConfig)
      throws Exception {
    JsonNode crParams = objectMapper.valueToTree(drConfig.getSpec());
    DrConfigSetDatabasesForm drConfigSetDatabasesForm =
        getDrConfigSetDatabasesFormFromCr(crParams, drConfig.getMetadata().getNamespace());
    drConfigSetDatabasesForm.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(drConfig));
    return drConfigSetDatabasesForm;
  }

  @VisibleForTesting
  DrConfigSetDatabasesForm getDrConfigSetDatabasesFormFromCr(JsonNode crParams, String namespace)
      throws Exception {
    Customer cust = getOperatorCustomer();
    String crSourceUniverseName = ((ObjectNode) crParams).get("sourceUniverse").asText();
    Universe sourceUniverse =
        getUniverseFromNameAndNamespace(cust.getId(), crSourceUniverseName, namespace);
    if (sourceUniverse == null) {
      throw new Exception("No universe found with name " + crSourceUniverseName);
    }
    TableType tableType = TableType.PGSQL_TABLE_TYPE;
    YBClientApi client = ybService.getUniverseClient(sourceUniverse);
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

  public DrConfigFailoverForm getDrConfigFailoverFormFromCr(DrConfig drConfig) throws Exception {
    DrConfigFailoverForm failoverForm =
        getDrConfigFailoverFormFromCr(drConfig, drConfig.getMetadata().getNamespace());
    failoverForm.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(drConfig));
    return failoverForm;
  }

  @VisibleForTesting
  DrConfigFailoverForm getDrConfigFailoverFormFromCr(DrConfig drConfig, String namespace)
      throws Exception {

    // Get the DR config model to find the current primary and replica universes
    UUID drConfigUUID = UUID.fromString(drConfig.getStatus().getResourceUUID());
    com.yugabyte.yw.models.DrConfig drConfigModel =
        com.yugabyte.yw.models.DrConfig.getOrBadRequest(drConfigUUID);
    XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();

    DrConfigFailoverForm failoverForm = new DrConfigFailoverForm();
    // drReplicaUniverseUuid is the current target (will become new primary)
    failoverForm.drReplicaUniverseUuid = xClusterConfig.getTargetUniverseUUID();
    // primaryUniverseUuid is the current source (old primary)
    failoverForm.primaryUniverseUuid = xClusterConfig.getSourceUniverseUUID();
    // namespaceIdSafetimeEpochUsMap is optional, leaving it null for unplanned failover

    return failoverForm;
  }

  public DrConfigSwitchoverForm getDrConfigSwitchoverFormFromCr(DrConfig drConfig)
      throws Exception {
    DrConfigSwitchoverForm switchoverForm =
        getDrConfigSwitchoverFormFromCr(drConfig, drConfig.getMetadata().getNamespace());
    switchoverForm.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(drConfig));
    return switchoverForm;
  }

  @VisibleForTesting
  DrConfigSwitchoverForm getDrConfigSwitchoverFormFromCr(DrConfig drConfig, String namespace)
      throws Exception {

    // Get the DR config model to find the current primary and replica universes
    UUID drConfigUUID = UUID.fromString(drConfig.getStatus().getResourceUUID());
    com.yugabyte.yw.models.DrConfig drConfigModel =
        com.yugabyte.yw.models.DrConfig.getOrBadRequest(drConfigUUID);
    XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();

    DrConfigSwitchoverForm switchoverForm = new DrConfigSwitchoverForm();
    // primaryUniverseUuid is the current source (will become new replica after switchover)
    switchoverForm.primaryUniverseUuid = xClusterConfig.getSourceUniverseUUID();
    // drReplicaUniverseUuid is the current target (will become new primary after switchover)
    switchoverForm.drReplicaUniverseUuid = xClusterConfig.getTargetUniverseUUID();

    return switchoverForm;
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

  public boolean createReleaseCr(
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
        return true;
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
        return false;
      }
      config.setDownloadConfig(downloadConfig);

      releaseSpec.setConfig(config);
      release.setSpec(releaseSpec);

      kubernetesClient.resources(Release.class).inNamespace(namespace).resource(release).create();
      return true;
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

  /**
   * The KMS config an imported universe's encryption at rest hangs off: the config its active
   * universe key is held under, falling back to the association recorded on the universe when there
   * is no key yet. Null when the universe has never had encryption at rest.
   */
  public static UUID getUniverseKmsConfigUuid(Universe universe) {
    KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(universe.getUniverseUUID());
    if (activeKey != null) {
      return activeKey.getConfigUuid();
    }
    EncryptionAtRestConfig earConfig = universe.getUniverseDetails().encryptionAtRestConfig;
    return earConfig == null ? null : earConfig.kmsConfigUUID;
  }

  /**
   * The credentials held in a KMS config's auth config, keyed by auth-config field name. Each one
   * has to be moved into a Kubernetes Secret before {@link #createKMSConfigCr} can reference it,
   * because the CRD only takes credentials by Secret reference. Fields that the config does not use
   * (an IAM profile, a managed identity, an instance principal, the unused auth type) are absent
   * from the auth config and so are absent here too.
   */
  public static Map<String, String> getKMSConfigSecretValues(KmsConfig cfg) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(cfg.getConfigUUID());
    if (authConfig == null) {
      throw new RuntimeException(
          String.format("KMS config %s has no auth config to import", cfg.getName()));
    }
    List<String> fields;
    switch (cfg.getKeyProvider()) {
      case AWS:
        fields =
            List.of(
                AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName,
                AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName);
        break;
      case GCP:
        fields = List.of(GcpKmsAuthConfigField.GCP_CONFIG.fieldName);
        break;
      case AZU:
        fields = List.of(AzuKmsAuthConfigField.CLIENT_SECRET.fieldName);
        break;
      case OCI:
        fields = List.of(OciKmsAuthConfigField.ociPrivateKeyContent.fieldName);
        break;
      case CIPHERTRUST:
        fields =
            List.of(
                CipherTrustKmsAuthConfigField.PASSWORD.fieldName,
                CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName);
        break;
      case HASHICORP:
        fields =
            List.of(
                HashicorpVaultConfigParams.HC_VAULT_TOKEN,
                HashicorpVaultConfigParams.HC_VAULT_SECRET_ID);
        break;
      default:
        throw new UnsupportedOperationException(
            String.format(
                "%s KMS is not yet supported via the Kubernetes operator",
                cfg.getKeyProvider().name()));
    }
    Map<String, String> values = new HashMap<>();
    for (String field : fields) {
      if (!authConfig.hasNonNull(field)) {
        continue;
      }
      JsonNode value = authConfig.get(field);
      // The GCP service account credentials are an object; every other credential is a string.
      values.put(field, value.isTextual() ? value.asText() : value.toString());
    }
    return values;
  }

  /**
   * Creates a KMSConfig custom resource for a KMS config that already exists in YBA, so that an
   * imported universe's encryption at rest keeps working under the operator. The spec is the
   * reverse of {@link #getKMSConfigFormDataFromCr}: the stored auth config is mapped back onto the
   * CRD fields, with every credential replaced by a reference to a Secret the caller has already
   * created.
   *
   * @param cfg the YBA KMS config to import
   * @param namespace the namespace to create the CR in
   * @param secretNames auth-config field name to the name of the Secret holding its value, as
   *     returned by {@link #getKMSConfigSecretValues}. The key within each Secret is the
   *     auth-config field name itself.
   */
  public void createKMSConfigCr(KmsConfig cfg, String namespace, Map<String, String> secretNames)
      throws Exception {
    String crName = kubernetesCompatName(cfg.getName());
    try (final KubernetesClient kubernetesClient =
        kubernetesClientFactory.getKubernetesClientWithConfig(getK8sClientConfig())) {
      if (kubernetesClient.resources(KMSConfig.class).inNamespace(namespace).withName(crName).get()
          != null) {
        log.info("KMS config {} already exists, skipping creation", crName);
        return;
      }
      KMSConfig kmsConfigCr = new KMSConfig();
      kmsConfigCr.setMetadata(
          new ObjectMetaBuilder()
              .withName(crName)
              .withNamespace(namespace)
              // The reconciler adds this finalizer to the configs it creates, so that deleting the
              // CR deletes the KMS config in YBA. An imported config is no different.
              .withFinalizers(YB_FINALIZER)
              .withAnnotations(
                  Map.ofEntries(
                      Map.entry(
                          ResourceAnnotationKeys.YBA_RESOURCE_ID, cfg.getConfigUUID().toString())))
              .build());
      kmsConfigCr.setSpec(buildKMSConfigSpec(cfg, namespace, secretNames));
      KMSConfigStatus status = new KMSConfigStatus();
      status.setResourceUUID(cfg.getConfigUUID().toString());
      status.setState("Ready");
      status.setMessage("KMS config ready");
      kmsConfigCr.setStatus(status);
      kubernetesClient
          .resources(KMSConfig.class)
          .inNamespace(namespace)
          .resource(kmsConfigCr)
          .create();
    } catch (Exception e) {
      throw new Exception(
          String.format("Unable to create KMS config: %s type: KMSConfig", cfg.getName()), e);
    }
  }

  /** Builds the KMSConfig CR spec from a YBA KMS config's stored auth config. */
  @VisibleForTesting
  KMSConfigSpec buildKMSConfigSpec(
      KmsConfig cfg, String namespace, Map<String, String> secretNames) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(cfg.getConfigUUID());
    if (authConfig == null) {
      throw new RuntimeException(
          String.format("KMS config %s has no auth config to import", cfg.getName()));
    }
    KeyProvider provider = cfg.getKeyProvider();
    ObjectNode spec = Json.newObject();
    spec.put("name", cfg.getName());
    spec.put("provider", provider.name());
    switch (provider) {
      case AWS:
        spec.set("aws", buildAwsSpec(authConfig, namespace, secretNames));
        break;
      case GCP:
        spec.set("gcp", buildGcpSpec(authConfig, namespace, secretNames));
        break;
      case AZU:
        spec.set("azure", buildAzureSpec(authConfig, namespace, secretNames));
        break;
      case OCI:
        spec.set("oci", buildOciSpec(authConfig, namespace, secretNames));
        break;
      case CIPHERTRUST:
        spec.set("cipherTrust", buildCiphertrustSpec(authConfig, namespace, secretNames));
        break;
      case HASHICORP:
        spec.set("vault", buildVaultSpec(authConfig, namespace, secretNames));
        break;
      default:
        throw new UnsupportedOperationException(
            String.format(
                "%s KMS is not yet supported via the Kubernetes operator", provider.name()));
    }
    return objectMapper.convertValue(spec, KMSConfigSpec.class);
  }

  private ObjectNode buildAwsSpec(
      ObjectNode authConfig, String namespace, Map<String, String> secretNames) {
    ObjectNode aws = Json.newObject();
    aws.put("region", authConfig.get(AwsKmsAuthConfigField.REGION.fieldName).asText());
    // Static credentials are only stored when the config does not use the host IAM profile.
    boolean useIAMProfile = !authConfig.hasNonNull(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName);
    aws.put("useIAMProfile", useIAMProfile);
    if (!useIAMProfile) {
      aws.set(
          "accessKeyIdSecret",
          buildSecretRef(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName, namespace, secretNames));
      aws.set(
          "secretAccessKeySecret",
          buildSecretRef(
              AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName, namespace, secretNames));
    }
    // cmkID is always set on an existing config, whether the customer supplied it or YBA created
    // the CMK. cmkPolicy is deliberately not carried over: it only applies while creating a CMK.
    copyText(authConfig, AwsKmsAuthConfigField.CMK_ID.fieldName, aws, "cmkID");
    copyText(authConfig, AwsKmsAuthConfigField.ENDPOINT.fieldName, aws, "endpoint");
    return aws;
  }

  private ObjectNode buildGcpSpec(
      ObjectNode authConfig, String namespace, Map<String, String> secretNames) {
    ObjectNode gcp = Json.newObject();
    gcp.put(
        "location",
        getTextOrDefault(authConfig, GcpKmsAuthConfigField.LOCATION_ID.fieldName, "global"));
    gcp.put("keyRingName", authConfig.get(GcpKmsAuthConfigField.KEY_RING_ID.fieldName).asText());
    gcp.put(
        "cryptoKeyName", authConfig.get(GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName).asText());
    gcp.put(
        "protectionLevel",
        getTextOrDefault(authConfig, GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName, "HSM"));
    copyText(authConfig, GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName, gcp, "endpoint");
    gcp.set(
        "credentialsSecret",
        buildSecretRef(GcpKmsAuthConfigField.GCP_CONFIG.fieldName, namespace, secretNames));
    return gcp;
  }

  private ObjectNode buildAzureSpec(
      ObjectNode authConfig, String namespace, Map<String, String> secretNames) {
    ObjectNode azure = Json.newObject();
    azure.put("clientID", authConfig.get(AzuKmsAuthConfigField.CLIENT_ID.fieldName).asText());
    azure.put("tenantID", authConfig.get(AzuKmsAuthConfigField.TENANT_ID.fieldName).asText());
    azure.put(
        "keyVaultURL", authConfig.get(AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName).asText());
    azure.put("keyName", authConfig.get(AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName).asText());
    azure.put(
        "keyAlgorithm",
        getTextOrDefault(authConfig, AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName, "RSA"));
    azure.put(
        "keySize", getIntOrDefault(authConfig, AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName, 2048));
    // A managed identity leaves the client secret out of the auth config entirely.
    boolean useManagedIdentity =
        !authConfig.hasNonNull(AzuKmsAuthConfigField.CLIENT_SECRET.fieldName);
    azure.put("useManagedIdentity", useManagedIdentity);
    if (!useManagedIdentity) {
      azure.set(
          "clientSecretSecret",
          buildSecretRef(AzuKmsAuthConfigField.CLIENT_SECRET.fieldName, namespace, secretNames));
    }
    return azure;
  }

  private ObjectNode buildOciSpec(
      ObjectNode authConfig, String namespace, Map<String, String> secretNames) {
    ObjectNode oci = Json.newObject();
    oci.put("region", authConfig.get(OciKmsAuthConfigField.ociRegion.fieldName).asText());
    oci.put(
        "compartmentOCID",
        authConfig.get(OciKmsAuthConfigField.ociCompartmentId.fieldName).asText());
    oci.put("vaultOCID", authConfig.get(OciKmsAuthConfigField.ociVaultId.fieldName).asText());
    oci.put(
        "keyName",
        getTextOrDefault(authConfig, OciKmsAuthConfigField.ociKeyName.fieldName, "yba-master-key"));
    // keyOCID is filled in by the backend once the key exists, so an existing config always has it.
    copyText(authConfig, OciKmsAuthConfigField.ociKeyOcid.fieldName, oci, "keyOCID");
    // The auth type is left blank for API-key configs created before instance-principal support.
    boolean useInstancePrincipal =
        OciKmsAuthType.INSTANCE_PRINCIPAL
            .name()
            .equals(
                getTextOrDefault(authConfig, OciKmsAuthConfigField.ociAuthType.fieldName, null));
    oci.put("useInstancePrincipal", useInstancePrincipal);
    if (!useInstancePrincipal) {
      oci.put("userOCID", authConfig.get(OciKmsAuthConfigField.ociUserId.fieldName).asText());
      oci.put("tenancyOCID", authConfig.get(OciKmsAuthConfigField.ociTenancyId.fieldName).asText());
      oci.put(
          "fingerprint", authConfig.get(OciKmsAuthConfigField.ociFingerprint.fieldName).asText());
      oci.set(
          "privateKeySecret",
          buildSecretRef(
              OciKmsAuthConfigField.ociPrivateKeyContent.fieldName, namespace, secretNames));
    }
    return oci;
  }

  private ObjectNode buildCiphertrustSpec(
      ObjectNode authConfig, String namespace, Map<String, String> secretNames) {
    ObjectNode cipherTrust = Json.newObject();
    cipherTrust.put(
        "managerURL",
        authConfig.get(CipherTrustKmsAuthConfigField.CIPHERTRUST_MANAGER_URL.fieldName).asText());
    cipherTrust.put(
        "keyName", authConfig.get(CipherTrustKmsAuthConfigField.KEY_NAME.fieldName).asText());
    cipherTrust.put(
        "keyAlgorithm",
        getTextOrDefault(authConfig, CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName, "AES"));
    cipherTrust.put(
        "keySize",
        getIntOrDefault(authConfig, CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName, 256));
    String authType =
        getTextOrDefault(authConfig, CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName, null);
    // The backend calls user-credentials auth PASSWORD; the CRD calls it USER_CREDENTIALS.
    if ("PASSWORD".equals(authType)) {
      cipherTrust.put("authType", "USER_CREDENTIALS");
      ObjectNode userCredentials = Json.newObject();
      userCredentials.put(
          "username", authConfig.get(CipherTrustKmsAuthConfigField.USERNAME.fieldName).asText());
      userCredentials.set(
          "passwordSecret",
          buildSecretRef(CipherTrustKmsAuthConfigField.PASSWORD.fieldName, namespace, secretNames));
      cipherTrust.set("userCredentials", userCredentials);
    } else if ("REFRESH_TOKEN".equals(authType)) {
      cipherTrust.put("authType", "REFRESH_TOKEN");
      cipherTrust.set(
          "refreshTokenSecret",
          buildSecretRef(
              CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName, namespace, secretNames));
    } else {
      throw new UnsupportedOperationException(
          "Unsupported auth type for CIPHERTRUST KMS via the Kubernetes operator: " + authType);
    }
    return cipherTrust;
  }

  private ObjectNode buildVaultSpec(
      ObjectNode authConfig, String namespace, Map<String, String> secretNames) {
    ObjectNode vault = Json.newObject();
    vault.put("address", authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ADDRESS).asText());
    vault.put(
        "keyName",
        getTextOrDefault(authConfig, HashicorpVaultConfigParams.HC_VAULT_KEY_NAME, "key_yugabyte"));
    vault.put(
        "secretEngine",
        getTextOrDefault(authConfig, HashicorpVaultConfigParams.HC_VAULT_ENGINE, "transit"));
    String mountPath =
        getTextOrDefault(authConfig, HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH, "transit/");
    if (authConfig.hasNonNull(HashicorpVaultConfigParams.HC_VAULT_TOKEN)) {
      vault.put("authType", "TOKEN");
      vault.set(
          "tokenSecret",
          buildSecretRef(HashicorpVaultConfigParams.HC_VAULT_TOKEN, namespace, secretNames));
    } else {
      vault.put("authType", "APPROLE");
      ObjectNode appRole = Json.newObject();
      appRole.put("roleID", authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID).asText());
      appRole.set(
          "secretIdSecret",
          buildSecretRef(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID, namespace, secretNames));
      vault.set("appRole", appRole);
      String authNamespace =
          getTextOrDefault(authConfig, HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE, null);
      if (authNamespace != null) {
        vault.put("authNamespace", authNamespace);
        // The CR holds a mount path relative to the auth namespace, which
        // getKMSConfigFormDataFromCr prefixes on the way back. Strip it so the CR round-trips to
        // the auth config it came from and the reconciler sees no change.
        String prefix = authNamespace.endsWith("/") ? authNamespace : authNamespace + "/";
        if (mountPath.startsWith(prefix)) {
          mountPath = mountPath.substring(prefix.length());
        }
      }
    }
    vault.put("mountPath", mountPath);
    return vault;
  }

  /**
   * Builds the CR's {name, namespace, key} reference to the Secret holding an auth-config field.
   */
  private static ObjectNode buildSecretRef(
      String field, String namespace, Map<String, String> secretNames) {
    String secretName = secretNames == null ? null : secretNames.get(field);
    if (StringUtils.isBlank(secretName)) {
      throw new RuntimeException(
          String.format("No Secret was created for the KMS config credential '%s'", field));
    }
    ObjectNode ref = Json.newObject();
    ref.put("name", secretName);
    ref.put("namespace", namespace);
    ref.put("key", field);
    return ref;
  }

  /** Copies an optional text field from the auth config onto the spec, under a new name. */
  private static void copyText(ObjectNode from, String fromField, ObjectNode to, String toField) {
    if (from.hasNonNull(fromField)) {
      to.put(toField, from.get(fromField).asText());
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
      spec.setUseTablespaces(params.useTablespaces);
      spec.setUseRoles(params.getUseRoles());
      spec.setUsePrivileges(params.getUsePrivileges());
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
      spec.setUseYbdbInbuiltYbc(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc());

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
          universeImporter.setReadReplicaTserverVolume(rr, firstReadReplica);
          universeImporter.setReadReplicaAzDeviceInfoOverrides(rr, firstReadReplica);
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

      // Volume configuration
      universeImporter.setTserverVolumeSpecFromUniverse(spec, universe);
      universeImporter.setTserverResourceSpecFromUniverse(spec, universe);
      universeImporter.setMasterResourceSpecFromUniverse(spec, universe);
      universeImporter.setMasterVolumeSpecFromUniverse(spec, universe);

      // Ybc throttle parameters
      universeImporter.setYbcThrottleParametersSpecFromUniverse(spec, universe);

      // Kubernetes overrides
      universeImporter.setKubernetesOverridesSpecFromUniverse(spec, universe);

      // UserIntent overrides
      universeImporter.setAzDeviceInfoOverridesSpecFromUniverse(spec, universe);

      // Encryption at rest
      universeImporter.setEncryptionAtRestSpecFromUniverse(spec, universe);

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

  /** Returns the backend {@link KeyProvider} for the given KMSConfig custom resource. */
  public KeyProvider getKMSConfigProvider(KMSConfig kmsConfig) {
    ObjectNode spec = objectMapper.valueToTree(kmsConfig.getSpec());
    JsonNode providerNode = spec.get("provider");
    if (providerNode == null || providerNode.isNull()) {
      throw new RuntimeException("KMS config provider is not set");
    }
    return KeyProvider.valueOf(providerNode.asText());
  }

  /**
   * Builds the KMS provider auth-config form data from the KMSConfig CR spec, resolving any
   * referenced Kubernetes Secrets. The returned ObjectNode contains the KMS config {@code name}
   * plus the provider-specific auth-config fields expected by the backend EncryptionAtRest services
   * (the same shape the REST API accepts as the request body).
   *
   * <p>HASHICORP (Vault) is implemented with TOKEN and APPROLE auth, AWS with static credentials or
   * the host IAM profile, GCP with a service account credentials JSON, AZU with a service principal
   * or managed identity, CIPHERTRUST with user credentials or a refresh token, and OCI with API-key
   * authentication; every other provider throws {@link UnsupportedOperationException} as an
   * explicit placeholder for future support.
   */
  public ObjectNode getKMSConfigFormDataFromCr(KMSConfig kmsConfig) {
    ObjectNode spec = objectMapper.valueToTree(kmsConfig.getSpec());
    KeyProvider provider = getKMSConfigProvider(kmsConfig);
    String resourceNamespace = kmsConfig.getMetadata().getNamespace();
    ObjectNode formData = Json.newObject();
    formData.put("name", spec.get("name").asText());
    switch (provider) {
      case HASHICORP:
        buildHashicorpAuthConfig(formData, spec.get("vault"), resourceNamespace);
        break;
      case AWS:
        buildAwsAuthConfig(formData, spec.get("aws"), resourceNamespace);
        break;
      case GCP:
        buildGcpAuthConfig(formData, spec.get("gcp"), resourceNamespace);
        break;
      case AZU:
        buildAzureAuthConfig(formData, spec.get("azure"), resourceNamespace);
        break;
      case CIPHERTRUST:
        buildCiphertrustAuthConfig(formData, spec.get("cipherTrust"), resourceNamespace);
        break;
      case OCI:
        buildOciAuthConfig(formData, spec.get("oci"), resourceNamespace);
        break;
      default:
        throw new UnsupportedOperationException(
            String.format(
                "%s KMS is not yet supported via the Kubernetes operator", provider.name()));
    }
    return formData;
  }

  private void buildAwsAuthConfig(ObjectNode formData, JsonNode aws, String resourceNamespace) {
    if (aws == null || aws.isNull()) {
      throw new RuntimeException("aws configuration is required for AWS KMS");
    }
    formData.put(AwsKmsAuthConfigField.REGION.fieldName, aws.get("region").asText());

    // The host IAM profile authenticates through the default AWS credential chain and omits the
    // static credentials; otherwise both of them are required.
    boolean useIAMProfile = aws.hasNonNull("useIAMProfile") && aws.get("useIAMProfile").asBoolean();
    if (!useIAMProfile) {
      JsonNode accessKeyIdSecret = aws.get("accessKeyIdSecret");
      JsonNode secretAccessKeySecret = aws.get("secretAccessKeySecret");
      if (accessKeyIdSecret == null
          || accessKeyIdSecret.isNull()
          || secretAccessKeySecret == null
          || secretAccessKeySecret.isNull()) {
        throw new RuntimeException(
            "accessKeyIdSecret and secretAccessKeySecret are required for AWS KMS unless"
                + " useIAMProfile is true");
      }
      formData.put(
          AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName,
          resolveSecretRef(accessKeyIdSecret, resourceNamespace));
      formData.put(
          AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName,
          resolveSecretRef(secretAccessKeySecret, resourceNamespace));
    }

    // cmkID and endpoint are optional. When cmkID is omitted YBA creates a CMK on the customer's
    // behalf; when endpoint is omitted the default AWS KMS endpoint is used.
    String cmkId = getTextOrDefault(aws, "cmkID", null);
    if (cmkId != null) {
      formData.put(AwsKmsAuthConfigField.CMK_ID.fieldName, cmkId);
    }
    String endpoint = getTextOrDefault(aws, "endpoint", null);
    if (endpoint != null) {
      formData.put(AwsKmsAuthConfigField.ENDPOINT.fieldName, endpoint);
    }

    // Optional custom CMK policy document, used by the backend only when it creates the CMK
    // (cmkID unset). Held in a Secret because it is a multi-line JSON document.
    JsonNode cmkPolicySecret = aws.get("cmkPolicySecret");
    if (cmkPolicySecret != null && !cmkPolicySecret.isNull()) {
      formData.put(
          AwsKmsAuthConfigField.CMK_POLICY.fieldName,
          resolveSecretRef(cmkPolicySecret, resourceNamespace));
    }
  }

  private void buildGcpAuthConfig(ObjectNode formData, JsonNode gcp, String resourceNamespace) {
    if (gcp == null || gcp.isNull()) {
      throw new RuntimeException("gcp configuration is required for GCP KMS");
    }
    formData.put(
        GcpKmsAuthConfigField.LOCATION_ID.fieldName, getTextOrDefault(gcp, "location", "global"));
    formData.put(GcpKmsAuthConfigField.KEY_RING_ID.fieldName, gcp.get("keyRingName").asText());
    formData.put(GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName, gcp.get("cryptoKeyName").asText());
    formData.put(
        GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName,
        getTextOrDefault(gcp, "protectionLevel", "HSM"));

    String endpoint = getTextOrDefault(gcp, "endpoint", null);
    if (endpoint != null) {
      formData.put(GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName, endpoint);
    }

    // The service account credentials JSON is stored as a nested object under GCP_CONFIG. The
    // project ID is read from this JSON (GCP_CONFIG.project_id) with no other source, so the
    // credentials are required.
    JsonNode credentialsSecret = gcp.get("credentialsSecret");
    if (credentialsSecret == null || credentialsSecret.isNull()) {
      throw new RuntimeException("credentialsSecret is required for GCP KMS");
    }
    String credentialsJson = resolveSecretRef(credentialsSecret, resourceNamespace);
    formData.set(GcpKmsAuthConfigField.GCP_CONFIG.fieldName, readJson(credentialsJson));
  }

  private JsonNode readJson(String json) {
    try {
      return objectMapper.readTree(json);
    } catch (Exception e) {
      throw new RuntimeException("GCP credentials Secret does not contain valid JSON", e);
    }
  }

  private void buildAzureAuthConfig(ObjectNode formData, JsonNode azure, String resourceNamespace) {
    if (azure == null || azure.isNull()) {
      throw new RuntimeException("azure configuration is required for AZU KMS");
    }
    formData.put(AzuKmsAuthConfigField.CLIENT_ID.fieldName, azure.get("clientID").asText());
    formData.put(AzuKmsAuthConfigField.TENANT_ID.fieldName, azure.get("tenantID").asText());
    formData.put(AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName, azure.get("keyVaultURL").asText());
    formData.put(AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName, azure.get("keyName").asText());
    formData.put(
        AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName,
        getTextOrDefault(azure, "keyAlgorithm", "RSA"));
    formData.put(
        AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName, getIntOrDefault(azure, "keySize", 2048));

    // A managed identity authenticates through DefaultAzureCredential and omits the client secret;
    // a service principal requires it. clientID and tenantID are required in both cases.
    boolean useManagedIdentity =
        azure.hasNonNull("useManagedIdentity") && azure.get("useManagedIdentity").asBoolean();
    if (!useManagedIdentity) {
      JsonNode clientSecretSecret = azure.get("clientSecretSecret");
      if (clientSecretSecret == null || clientSecretSecret.isNull()) {
        throw new RuntimeException(
            "clientSecretSecret is required for AZU KMS unless useManagedIdentity is true");
      }
      formData.put(
          AzuKmsAuthConfigField.CLIENT_SECRET.fieldName,
          resolveSecretRef(clientSecretSecret, resourceNamespace));
    }
  }

  private static int getIntOrDefault(JsonNode node, String field, int defaultValue) {
    JsonNode value = node.get(field);
    if (value == null || value.isNull()) {
      return defaultValue;
    }
    return value.asInt();
  }

  private void buildCiphertrustAuthConfig(
      ObjectNode formData, JsonNode cipherTrust, String resourceNamespace) {
    if (cipherTrust == null || cipherTrust.isNull()) {
      throw new RuntimeException("cipherTrust configuration is required for CIPHERTRUST KMS");
    }
    formData.put(
        CipherTrustKmsAuthConfigField.CIPHERTRUST_MANAGER_URL.fieldName,
        cipherTrust.get("managerURL").asText());
    formData.put(
        CipherTrustKmsAuthConfigField.KEY_NAME.fieldName, cipherTrust.get("keyName").asText());
    formData.put(
        CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName,
        getTextOrDefault(cipherTrust, "keyAlgorithm", "AES"));
    formData.put(
        CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName,
        getIntOrDefault(cipherTrust, "keySize", 256));

    String authType = getTextOrDefault(cipherTrust, "authType", null);
    if ("USER_CREDENTIALS".equals(authType)) {
      JsonNode userCredentials = cipherTrust.get("userCredentials");
      if (userCredentials == null || userCredentials.isNull()) {
        throw new RuntimeException(
            "userCredentials is required for CIPHERTRUST KMS with USER_CREDENTIALS auth");
      }
      // The backend represents user-credentials auth as the PASSWORD auth type.
      formData.put(CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName, "PASSWORD");
      formData.put(
          CipherTrustKmsAuthConfigField.USERNAME.fieldName,
          userCredentials.get("username").asText());
      JsonNode passwordSecret = userCredentials.get("passwordSecret");
      if (passwordSecret == null || passwordSecret.isNull()) {
        throw new RuntimeException(
            "passwordSecret is required for CIPHERTRUST KMS with USER_CREDENTIALS auth");
      }
      formData.put(
          CipherTrustKmsAuthConfigField.PASSWORD.fieldName,
          resolveSecretRef(passwordSecret, resourceNamespace));
    } else if ("REFRESH_TOKEN".equals(authType)) {
      JsonNode refreshTokenSecret = cipherTrust.get("refreshTokenSecret");
      if (refreshTokenSecret == null || refreshTokenSecret.isNull()) {
        throw new RuntimeException(
            "refreshTokenSecret is required for CIPHERTRUST KMS with REFRESH_TOKEN auth");
      }
      formData.put(CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName, "REFRESH_TOKEN");
      formData.put(
          CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName,
          resolveSecretRef(refreshTokenSecret, resourceNamespace));
    } else {
      throw new UnsupportedOperationException(
          "Unsupported auth type for CIPHERTRUST KMS via the Kubernetes operator: " + authType);
    }
  }

  // OCI is exposed with API-key authentication (the default when ociAuthType is absent) and with
  // the OCI Instance Principal of the YBA host.
  private void buildOciAuthConfig(ObjectNode formData, JsonNode oci, String resourceNamespace) {
    if (oci == null || oci.isNull()) {
      throw new RuntimeException("oci configuration is required for OCI KMS");
    }
    formData.put(OciKmsAuthConfigField.ociRegion.fieldName, oci.get("region").asText());
    formData.put(
        OciKmsAuthConfigField.ociCompartmentId.fieldName, oci.get("compartmentOCID").asText());
    formData.put(OciKmsAuthConfigField.ociVaultId.fieldName, oci.get("vaultOCID").asText());
    formData.put(
        OciKmsAuthConfigField.ociKeyName.fieldName,
        getTextOrDefault(oci, "keyName", "yba-master-key"));

    // The instance principal is resolved from the host metadata service and omits the API-key
    // credentials; otherwise all four of them are required.
    boolean useInstancePrincipal =
        oci.hasNonNull("useInstancePrincipal") && oci.get("useInstancePrincipal").asBoolean();
    if (useInstancePrincipal) {
      formData.put(
          OciKmsAuthConfigField.ociAuthType.fieldName, OciKmsAuthType.INSTANCE_PRINCIPAL.name());
    } else {
      JsonNode userOcid = oci.get("userOCID");
      JsonNode tenancyOcid = oci.get("tenancyOCID");
      JsonNode fingerprint = oci.get("fingerprint");
      JsonNode privateKeySecret = oci.get("privateKeySecret");
      if (userOcid == null
          || userOcid.isNull()
          || tenancyOcid == null
          || tenancyOcid.isNull()
          || fingerprint == null
          || fingerprint.isNull()
          || privateKeySecret == null
          || privateKeySecret.isNull()) {
        throw new RuntimeException(
            "userOCID, tenancyOCID, fingerprint and privateKeySecret are required for OCI KMS"
                + " unless useInstancePrincipal is true");
      }
      // ociAuthType is deliberately left unset for API_KEY: it is the backend default when the
      // field is blank, so configs created before instance-principal support keep an identical
      // auth config and do not look changed to the reconciler.
      formData.put(OciKmsAuthConfigField.ociUserId.fieldName, userOcid.asText());
      formData.put(OciKmsAuthConfigField.ociTenancyId.fieldName, tenancyOcid.asText());
      formData.put(OciKmsAuthConfigField.ociFingerprint.fieldName, fingerprint.asText());
      formData.put(
          OciKmsAuthConfigField.ociPrivateKeyContent.fieldName,
          resolveSecretRef(privateKeySecret, resourceNamespace));
    }

    // keyOCID is optional: when omitted YBA creates a key with keyName in the vault.
    String keyOcid = getTextOrDefault(oci, "keyOCID", null);
    if (keyOcid != null) {
      formData.put(OciKmsAuthConfigField.ociKeyOcid.fieldName, keyOcid);
    }
  }

  private void buildHashicorpAuthConfig(
      ObjectNode formData, JsonNode vault, String resourceNamespace) {
    if (vault == null || vault.isNull()) {
      throw new RuntimeException("vault configuration is required for HASHICORP KMS");
    }
    formData.put(HashicorpVaultConfigParams.HC_VAULT_ADDRESS, vault.get("address").asText());
    formData.put(
        HashicorpVaultConfigParams.HC_VAULT_KEY_NAME,
        getTextOrDefault(vault, "keyName", "key_yugabyte"));
    formData.put(
        HashicorpVaultConfigParams.HC_VAULT_ENGINE,
        getTextOrDefault(vault, "secretEngine", "transit"));

    String mountPath = getTextOrDefault(vault, "mountPath", "transit/");

    String authType = getTextOrDefault(vault, "authType", null);
    if ("TOKEN".equals(authType)) {
      JsonNode tokenSecret = vault.get("tokenSecret");
      if (tokenSecret == null || tokenSecret.isNull()) {
        throw new RuntimeException("tokenSecret is required for HASHICORP KMS with TOKEN auth");
      }
      formData.put(
          HashicorpVaultConfigParams.HC_VAULT_TOKEN,
          resolveSecretRef(tokenSecret, resourceNamespace));
    } else if ("APPROLE".equals(authType)) {
      JsonNode appRole = vault.get("appRole");
      if (appRole == null || appRole.isNull()) {
        throw new RuntimeException("appRole is required for HASHICORP KMS with APPROLE auth");
      }
      formData.put(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID, appRole.get("roleID").asText());
      JsonNode secretIdSecret = appRole.get("secretIdSecret");
      if (secretIdSecret == null || secretIdSecret.isNull()) {
        throw new RuntimeException(
            "secretIdSecret is required for HASHICORP KMS with APPROLE auth");
      }
      formData.put(
          HashicorpVaultConfigParams.HC_VAULT_SECRET_ID,
          resolveSecretRef(secretIdSecret, resourceNamespace));

      // authNamespace is only meaningful for APPROLE auth. The backend expects the transit mount
      // path to be namespace-qualified (it strips the auth-namespace prefix before use), so the CR
      // takes a mountPath relative to the Vault namespace and we prefix it here.
      String authNamespace = getTextOrDefault(vault, "authNamespace", null);
      if (authNamespace != null) {
        formData.put(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE, authNamespace);
        String prefix = authNamespace.endsWith("/") ? authNamespace : authNamespace + "/";
        if (!mountPath.startsWith(prefix)) {
          mountPath = prefix + mountPath;
        }
      }
    } else {
      throw new UnsupportedOperationException(
          "Unsupported auth type for HASHICORP KMS via the Kubernetes operator: " + authType);
    }

    formData.put(HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH, mountPath);
  }

  /**
   * Resolves a {name, namespace, key} Secret reference from the CR to the secret's string value.
   */
  private String resolveSecretRef(JsonNode secretRef, String defaultNamespace) {
    String name = secretRef.get("name").asText();
    String key = secretRef.get("key").asText();
    String namespace =
        secretRef.hasNonNull("namespace") ? secretRef.get("namespace").asText() : defaultNamespace;
    String value = parseSecretForKey(getSecret(name, namespace), key);
    if (value == null) {
      throw new RuntimeException(
          String.format("Could not resolve key '%s' from secret '%s'", key, name));
    }
    return value;
  }

  private static String getTextOrDefault(JsonNode node, String field, String defaultValue) {
    JsonNode value = node.get(field);
    if (value == null || value.isNull()) {
      return defaultValue;
    }
    return value.asText();
  }

  public boolean requiresDrConfigDatabaseUpdate(DrConfig drConfig, XClusterConfig xClusterConfig) {
    try {
      if (xClusterConfig == null) {
        return false;
      }

      // Get database names from CR spec
      List<String> specDatabases = drConfig.getSpec().getDatabases();
      if (specDatabases == null) {
        specDatabases = Collections.emptyList();
      }

      // Get current database IDs from xCluster config
      java.util.Set<String> currentDbIds = xClusterConfig.getDbIds();
      if (currentDbIds == null) {
        currentDbIds = Collections.emptySet();
      }

      // Quick size check first
      if (specDatabases.size() != currentDbIds.size()) {
        return true;
      }

      // If both are empty, no update needed
      if (specDatabases.isEmpty() && currentDbIds.isEmpty()) {
        return false;
      }

      // Resolve spec database names to IDs
      Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
      TableType tableType = TableType.PGSQL_TABLE_TYPE;
      YBClientApi client = ybService.getUniverseClient(sourceUniverse);
      Map<String, String> namespaceNameToIdMap =
          UniverseTaskBase.getKeyspaceNameKeyspaceIdMap(client, tableType);

      java.util.Set<String> specDbIds = new java.util.HashSet<>();
      for (String dbName : specDatabases) {
        String namespaceId = namespaceNameToIdMap.get(dbName.trim());
        if (namespaceId != null) {
          specDbIds.add(namespaceId);
        } else {
          // Database name not found - this might be a new database or typo
          // Treat as requiring update so the actual task can handle the error
          log.warn("Database '{}' not found in source universe, will attempt update", dbName);
          return true;
        }
      }

      // Compare the resolved IDs with current IDs
      return !specDbIds.equals(currentDbIds);
    } catch (Exception e) {
      log.warn("Error checking DR config database update requirement: {}", e.getMessage());
      // On error, return false to avoid unnecessary updates
      return false;
    }
  }

  public RestoreSnapshotScheduleParams getRestoreSnapshotScheduleParamsFromCr(
      PitrRestore pitrRestore) throws Exception {
    RestoreSnapshotScheduleParams restoreParams =
        getRestoreSnapshotScheduleParamsFromCr(
            pitrRestore, pitrRestore.getMetadata().getNamespace());
    return restoreParams;
  }

  @VisibleForTesting
  RestoreSnapshotScheduleParams getRestoreSnapshotScheduleParamsFromCr(
      PitrRestore pitrRestore, String namespace) throws Exception {
    Customer cust = getOperatorCustomer();

    // Get universe from CR
    String universeName = pitrRestore.getSpec().getUniverse();
    Universe universe = getUniverseFromNameAndNamespace(cust.getId(), universeName, namespace);
    if (universe == null) {
      throw new Exception("No universe found with name " + universeName);
    }

    // Get PitrConfig from database by name
    String pitrConfigName = pitrRestore.getSpec().getPitrConfig();
    Optional<com.yugabyte.yw.models.PitrConfig> pitrConfigOpt =
        com.yugabyte.yw.models.PitrConfig.maybeGetByName(
            universe.getUniverseUUID(), pitrConfigName);
    if (pitrConfigOpt.isEmpty()) {
      throw new Exception(
          "No PITR config found with name " + pitrConfigName + " for universe " + universeName);
    }
    com.yugabyte.yw.models.PitrConfig pitrConfig = pitrConfigOpt.get();

    // Parse restore time from ISO 8601 format to millis
    String restoreTimeStr = pitrRestore.getSpec().getRestoreTime();
    long restoreTimeInMillis = OffsetDateTime.parse(restoreTimeStr).toInstant().toEpochMilli();

    // Build RestoreSnapshotScheduleParams
    RestoreSnapshotScheduleParams taskParams = new RestoreSnapshotScheduleParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.pitrConfigUUID = pitrConfig.getUuid();
    taskParams.restoreTimeInMillis = restoreTimeInMillis;

    return taskParams;
  }

  /**
   * Creates a DrConfigRestartForm from the DrConfig CR. Used when restarting DR after failover or
   * when DR is in halted state.
   */
  public DrConfigRestartForm getDrConfigRestartFormFromCr(
      DrConfig drConfig, SharedIndexInformer<StorageConfig> scInformer) throws Exception {
    DrConfigRestartForm restartForm =
        getDrConfigRestartFormFromCr(drConfig, drConfig.getMetadata().getNamespace(), scInformer);
    restartForm.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(drConfig));
    return restartForm;
  }

  @VisibleForTesting
  DrConfigRestartForm getDrConfigRestartFormFromCr(
      DrConfig drConfig, String namespace, SharedIndexInformer<StorageConfig> scInformer)
      throws Exception {
    Customer cust = getOperatorCustomer();

    // Get the DR config model to access current state
    UUID drConfigUUID = UUID.fromString(drConfig.getStatus().getResourceUUID());
    com.yugabyte.yw.models.DrConfig drConfigModel =
        com.yugabyte.yw.models.DrConfig.getOrBadRequest(drConfigUUID);

    // Get source universe for database ID resolution
    String crSourceUniverseName = drConfig.getSpec().getSourceUniverse();
    Universe sourceUniverse =
        getUniverseFromNameAndNamespace(cust.getId(), crSourceUniverseName, namespace);
    if (sourceUniverse == null) {
      throw new Exception("No universe found with name " + crSourceUniverseName);
    }

    // Resolve database names to IDs
    TableType tableType = TableType.PGSQL_TABLE_TYPE;
    YBClientApi client = ybService.getUniverseClient(sourceUniverse);
    Map<String, String> namespaceNameNamespaceIdMap =
        UniverseTaskBase.getKeyspaceNameKeyspaceIdMap(client, tableType);

    List<String> specDatabases = drConfig.getSpec().getDatabases();
    Set<String> dbIds = new HashSet<>();
    if (specDatabases != null) {
      for (String dbName : specDatabases) {
        String namespaceId = namespaceNameNamespaceIdMap.get(dbName.trim());
        if (namespaceId != null) {
          dbIds.add(namespaceId);
        }
      }
    }

    // Get storage config UUID
    String crStorageConfig = drConfig.getSpec().getStorageConfig();
    UUID storageConfigUUID = getStorageConfigUUIDFromName(crStorageConfig, scInformer);
    if (storageConfigUUID == null) {
      throw new Exception("No storage config found with name " + crStorageConfig);
    }

    DrConfigRestartForm restartForm = new DrConfigRestartForm();
    restartForm.dbs = dbIds;

    // Set bootstrap params from storage config
    BootstrapParams.BootstrapBackupParams backupRequestParams =
        new BootstrapParams.BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = storageConfigUUID;
    restartForm.bootstrapParams = new RestartBootstrapParams();
    restartForm.bootstrapParams.backupRequestParams = backupRequestParams;

    return restartForm;
  }

  /**
   * Creates a DrConfigReplaceReplicaForm from the DrConfig CR. Used when changing the
   * target/replica universe while keeping the source the same.
   */
  public DrConfigReplaceReplicaForm getDrConfigReplaceReplicaFormFromCr(
      DrConfig drConfig, SharedIndexInformer<StorageConfig> scInformer) throws Exception {
    DrConfigReplaceReplicaForm replaceReplicaForm =
        getDrConfigReplaceReplicaFormFromCr(
            drConfig, drConfig.getMetadata().getNamespace(), scInformer);
    replaceReplicaForm.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(drConfig));
    return replaceReplicaForm;
  }

  @VisibleForTesting
  DrConfigReplaceReplicaForm getDrConfigReplaceReplicaFormFromCr(
      DrConfig drConfig, String namespace, SharedIndexInformer<StorageConfig> scInformer)
      throws Exception {
    Customer cust = getOperatorCustomer();

    // Get the DR config model to find the current primary universe
    UUID drConfigUUID = UUID.fromString(drConfig.getStatus().getResourceUUID());
    com.yugabyte.yw.models.DrConfig drConfigModel =
        com.yugabyte.yw.models.DrConfig.getOrBadRequest(drConfigUUID);
    XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();

    // Get source universe (primary) - this stays the same
    String crSourceUniverseName = drConfig.getSpec().getSourceUniverse();
    Universe sourceUniverse =
        getUniverseFromNameAndNamespace(cust.getId(), crSourceUniverseName, namespace);
    if (sourceUniverse == null) {
      throw new Exception("No universe found with name " + crSourceUniverseName);
    }

    // Get new target universe (new replica)
    String crTargetUniverseName = drConfig.getSpec().getTargetUniverse();
    Universe newTargetUniverse =
        getUniverseFromNameAndNamespace(cust.getId(), crTargetUniverseName, namespace);
    if (newTargetUniverse == null) {
      throw new Exception("No universe found with name " + crTargetUniverseName);
    }

    // Get storage config UUID
    String crStorageConfig = drConfig.getSpec().getStorageConfig();
    UUID storageConfigUUID = getStorageConfigUUIDFromName(crStorageConfig, scInformer);
    if (storageConfigUUID == null) {
      throw new Exception("No storage config found with name " + crStorageConfig);
    }

    DrConfigReplaceReplicaForm replaceReplicaForm = new DrConfigReplaceReplicaForm();
    // primaryUniverseUuid is the current source (stays the same)
    replaceReplicaForm.primaryUniverseUuid = sourceUniverse.getUniverseUUID();
    // drReplicaUniverseUuid is the new target (replacement replica)
    replaceReplicaForm.drReplicaUniverseUuid = newTargetUniverse.getUniverseUUID();

    // Set bootstrap params from storage config
    BootstrapParams.BootstrapBackupParams backupRequestParams =
        new BootstrapParams.BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = storageConfigUUID;
    replaceReplicaForm.bootstrapParams = new RestartBootstrapParams();
    replaceReplicaForm.bootstrapParams.backupRequestParams = backupRequestParams;

    return replaceReplicaForm;
  }
}
