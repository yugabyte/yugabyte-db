package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.common.operator.helpers.KubernetesOverridesSerializer;
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
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.ConfigBuilder;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;
import io.yugabyte.operator.v1alpha1.YBUniverse;
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

  @Inject
  public OperatorUtils(RuntimeConfGetter confGetter) {
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
      UserIntent currentUserIntent, int newNumNodes, DeviceInfo newDeviceInfo) {
    return !(currentUserIntent.numNodes == newNumNodes)
        || !currentUserIntent.deviceInfo.volumeSize.equals(newDeviceInfo.volumeSize);
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

    Provider provider =
        Provider.getOrBadRequest(cust.getUuid(), UUID.fromString(currentUserIntent.provider));
    // Get all required params
    SpecificGFlags specGFlags = getGFlagsFromSpec(ybUniverse, provider);
    String incomingOverrides =
        getKubernetesOverridesString(ybUniverse.getSpec().getKubernetesOverrides());
    String incomingYbSoftwareVersion = ybUniverse.getSpec().getYbSoftwareVersion();
    DeviceInfo incomingDeviceInfo = mapDeviceInfo(ybUniverse.getSpec().getDeviceInfo());
    int incomingNumNodes = (int) ybUniverse.getSpec().getNumNodes().longValue();

    if (prevTaskToRerun != null) {
      TaskType specificTaskTypeToRerun = prevTaskToRerun.getTaskType();
      switch (specificTaskTypeToRerun) {
        case EditKubernetesUniverse:
          UniverseDefinitionTaskParams prevTaskParams =
              Json.fromJson(prevTaskToRerun.getTaskParams(), UniverseDefinitionTaskParams.class);
          return shouldUpdateYbUniverse(
              prevTaskParams.getPrimaryCluster().userIntent, incomingNumNodes, incomingDeviceInfo);
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

    return (!StringUtils.equals(incomingOverrides, currentUserIntent.universeOverrides))
        || checkIfGFlagsChanged(
            u,
            u.getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .specificGFlags /*Current gflags */,
            specGFlags)
        || shouldUpdateYbUniverse(currentUserIntent, incomingNumNodes, incomingDeviceInfo)
        || !StringUtils.equals(currentUserIntent.ybSoftwareVersion, incomingYbSoftwareVersion);
  }
}
