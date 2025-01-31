package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.common.operator.helpers.KubernetesOverridesSerializer;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.ConfigBuilder;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.releasespec.config.DownloadConfig;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
public class OperatorUtils {

  private final RuntimeConfGetter confGetter;
  private final String namespace;
  private final Config k8sClientConfig;

  private ReleaseManager releaseManager;

  @Inject
  public OperatorUtils(RuntimeConfGetter confGetter, ReleaseManager releaseManager) {
    this.releaseManager = releaseManager;
    this.confGetter = confGetter;
    namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    ConfigBuilder confBuilder = new ConfigBuilder();
    if (namespace == null || namespace.trim().isEmpty()) {
      confBuilder.withNamespace(null);
    } else {
      confBuilder.withNamespace(namespace);
    }
    k8sClientConfig = confBuilder.build();
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

  public Universe getUniverseFromNameAndNamespace(
      Long customerId, String universeName, String namespace) throws Exception {
    KubernetesResourceDetails ybUniverseResourceDetails = new KubernetesResourceDetails();
    ybUniverseResourceDetails.name = universeName;
    ybUniverseResourceDetails.namespace = namespace;
    YBUniverse ybUniverse = getYBUniverse(ybUniverseResourceDetails);
    Optional<Universe> universe =
        Universe.maybeGetUniverseByName(customerId, getYbaUniverseName(ybUniverse));
    if (universe.isPresent()) {
      return universe.get();
    }
    return null;
  }

  public YBUniverse getYBUniverse(KubernetesResourceDetails name) throws Exception {
    try (final KubernetesClient kubernetesClient =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
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

  public static String getYbaUniverseName(YBUniverse ybUniverse) {
    String name = ybUniverse.getMetadata().getName();
    String namespace = ybUniverse.getMetadata().getNamespace();
    String uid = ybUniverse.getMetadata().getUid();
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

  public String getKubernetesOverridesString(
      io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides kubernetesOverrides) {
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
            || shouldUpdateYbUniverse(
                currentUserIntent, incomingNumNodes, incomingDeviceInfo, incomingMasterDeviceInfo);
    log.trace("nodes mismatch: {}", mismatch);
    mismatch =
        mismatch
            || !StringUtils.equals(currentUserIntent.ybSoftwareVersion, incomingYbSoftwareVersion);
    log.trace("version mismatch: {}", mismatch);
    mismatch = mismatch || pauseChangeRequired;
    log.trace("pause mismatch: {}", mismatch);
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
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
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
}
