/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.certmgmt.providers.VaultPKI;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.DiskIncreaseFormData;
import com.yugabyte.yw.forms.TlsConfigUpdateParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.Status;

public class UniverseCRUDHandler {

  private static final Logger LOG = LoggerFactory.getLogger(UniverseCRUDHandler.class);

  @Inject Commissioner commissioner;

  @Inject EncryptionAtRestManager keyManager;

  @Inject play.Configuration appConfig;

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  @Inject KubernetesManagerFactory kubernetesManagerFactory;
  @Inject PasswordPolicyService passwordPolicyService;

  @Inject UpgradeUniverseHandler upgradeUniverseHandler;

  private static enum OpType {
    CONFIGURE,
    CREATE,
    UPDATE
  }

  /**
   * Function to Trim keys and values of the passed map.
   *
   * @param data key value pairs.
   * @return key value pairs with trim keys and values.
   */
  @VisibleForTesting
  public static Map<String, String> trimFlags(Map<String, String> data) {
    Map<String, String> trimData = new HashMap<>();
    for (Map.Entry<String, String> intent : data.entrySet()) {
      String key = intent.getKey();
      String value = intent.getValue();
      trimData.put(key.trim(), value.trim());
    }
    return trimData;
  }

  public void configure(Customer customer, UniverseConfigureTaskParams taskParams) {
    if (taskParams.currentClusterType == null) {
      throw new PlatformServiceException(BAD_REQUEST, "currentClusterType must be set");
    }
    if (taskParams.clusterOperation == null) {
      throw new PlatformServiceException(BAD_REQUEST, "clusterOperation must be set");
    }

    // TODO(Rahul): When we support multiple read only clusters, change clusterType to cluster
    //  uuid.
    Cluster c =
        taskParams.getCurrentClusterType().equals(UniverseDefinitionTaskParams.ClusterType.PRIMARY)
            ? taskParams.getPrimaryCluster()
            : taskParams.getReadOnlyClusters().get(0);
    UniverseDefinitionTaskParams.UserIntent primaryIntent = c.userIntent;

    checkGeoPartitioningParameters(customer, taskParams, OpType.CONFIGURE);

    primaryIntent.masterGFlags = trimFlags(primaryIntent.masterGFlags);
    primaryIntent.tserverGFlags = trimFlags(primaryIntent.tserverGFlags);
    if (StringUtils.isEmpty(primaryIntent.accessKeyCode)) {
      primaryIntent.accessKeyCode = appConfig.getString("yb.security.default.access.key");
    }
    if (PlacementInfoUtil.checkIfNodeParamsValid(taskParams, c)) {
      try {
        PlacementInfoUtil.updateUniverseDefinition(taskParams, customer.getCustomerId(), c.uuid);
      } catch (IllegalStateException | UnsupportedOperationException e) {
        throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
      }
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid Node/AZ combination for given instance type " + c.userIntent.instanceType);
    }
  }

  private void checkGeoPartitioningParameters(
      Customer customer, UniverseDefinitionTaskParams taskParams, OpType op) {

    UUID defaultRegionUUID = PlacementInfoUtil.getDefaultRegion(taskParams);
    if (defaultRegionUUID != null) {
      UserIntent intent = taskParams.getPrimaryCluster().userIntent;
      Region defaultRegion =
          Region.getOrBadRequest(
              customer.getUuid(), UUID.fromString(intent.provider), defaultRegionUUID);
      if (!intent.regionList.contains(defaultRegionUUID)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Default region " + defaultRegion + " not in user region list.");
      }

      if (op == OpType.CREATE || op == OpType.UPDATE) {
        int nodesInDefRegion =
            (int)
                taskParams
                    .nodeDetailsSet
                    .stream()
                    .filter(n -> n.isActive() && defaultRegion.code.equals(n.cloudInfo.region))
                    .count();
        if (nodesInDefRegion < intent.replicationFactor) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              String.format(
                  "Could not pick %d masters, only %d nodes available in default region %s.",
                  intent.replicationFactor, nodesInDefRegion, defaultRegion.name));
        }
      }
    }
  }

  // Creates RootCA certificate if not provided and creates client certificate
  public void checkForCertificates(Customer customer, UniverseDefinitionTaskParams taskParams) {
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    CertificateInfo cert;

    if (primaryCluster.userIntent.enableNodeToNodeEncrypt) {
      if (taskParams.rootCA != null) {
        cert = CertificateInfo.get(taskParams.rootCA);

        if (cert.certType == CertConfigType.CustomServerCert) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "CustomServerCert are only supported for Client to Server Communication.");
        }

        if (cert.certType == CertConfigType.CustomCertHostPath) {
          if (!taskParams
              .getPrimaryCluster()
              .userIntent
              .providerType
              .equals(Common.CloudType.onprem)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "CustomCertHostPath certificates are only supported for onprem providers.");
          }
        }

        if (cert.certType == CertConfigType.HashicorpVault) {
          try {
            VaultPKI certProvider = VaultPKI.getVaultPKIInstance(cert);
            certProvider.dumpCACertBundle(
                runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"),
                customer.uuid);
          } catch (Exception e) {
            throw new PlatformServiceException(
                INTERNAL_SERVER_ERROR,
                String.format(
                    "Error while dumping certs from Vault for certificate: {}", taskParams.rootCA));
          }
        }
      } else {
        // create self-signed rootCA in case it is not provided by the user.
        taskParams.rootCA =
            CertificateHelper.createRootCA(
                taskParams.nodePrefix,
                customer.uuid,
                runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"));
      }
      checkValidRootCA(taskParams.rootCA);
    }

    if (primaryCluster.userIntent.enableClientToNodeEncrypt) {
      if (taskParams.clientRootCA == null) {
        if (taskParams.rootCA != null && taskParams.rootAndClientRootCASame) {
          // Setting ClientRootCA to RootCA in case rootAndClientRootCA is true
          taskParams.clientRootCA = taskParams.rootCA;
        } else {
          // create self-signed clientRootCA in case it is not provided by the user
          // and root and clientRoot CA needs to be different
          taskParams.clientRootCA =
              CertificateHelper.createClientRootCA(
                  taskParams.nodePrefix,
                  customer.uuid,
                  runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"));
        }
      }

      cert = CertificateInfo.get(taskParams.clientRootCA);
      if (cert.certType == CertConfigType.CustomCertHostPath) {
        if (!taskParams
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(Common.CloudType.onprem)) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "CustomCertHostPath certificates are only supported for onprem providers.");
        }
      }

      if (cert.certType == CertConfigType.HashicorpVault) {
        try {
          VaultPKI certProvider = VaultPKI.getVaultPKIInstance(cert);
          certProvider.dumpCACertBundle(
              runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"),
              customer.uuid);
        } catch (Exception e) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              String.format(
                  "Error while dumping certs from Vault for certificate: {}",
                  taskParams.clientRootCA));
        }
      }

      checkValidRootCA(taskParams.clientRootCA);

      // Setting rootCA to ClientRootCA in case node to node encryption is disabled.
      // This is necessary to set to ensure backward compatibility as existing parts of
      // codebase (kubernetes) uses rootCA for Client to Node Encryption
      if (taskParams.rootCA == null && taskParams.rootAndClientRootCASame) {
        taskParams.rootCA = taskParams.clientRootCA;
      }

      // Generate client certs if rootAndClientRootCASame is true and rootCA is self-signed.
      // This is there only for legacy support, no need if rootCA and clientRootCA are different.
      if (taskParams.rootAndClientRootCASame) {
        CertificateInfo rootCert = CertificateInfo.get(taskParams.rootCA);
        if (rootCert.certType == CertConfigType.SelfSigned
            || rootCert.certType == CertConfigType.HashicorpVault) {
          CertificateHelper.createClientCertificate(
              taskParams.rootCA,
              String.format(
                  CertificateHelper.CERT_PATH,
                  runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"),
                  customer.uuid.toString(),
                  taskParams.rootCA.toString()),
              CertificateHelper.DEFAULT_CLIENT,
              null,
              null);
        }
      }
    }
  }

  public UniverseResp createUniverse(Customer customer, UniverseDefinitionTaskParams taskParams) {
    LOG.info("Create for {}.", customer.uuid);

    // Get the user submitted form data.
    if (taskParams.getPrimaryCluster() != null
        && !Util.isValidUniverseNameFormat(
            taskParams.getPrimaryCluster().userIntent.universeName)) {
      throw new PlatformServiceException(BAD_REQUEST, Util.UNIVERSE_NAME_ERROR_MESG);
    }

    if (!taskParams.rootAndClientRootCASame
        && taskParams
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(Common.CloudType.kubernetes)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "root and clientRootCA cannot be different for Kubernetes env.");
    }

    for (Cluster c : taskParams.clusters) {
      Provider provider = Provider.getOrBadRequest(UUID.fromString(c.userIntent.provider));
      // Set the provider code.
      c.userIntent.providerType = Common.CloudType.valueOf(provider.code);
      c.validate();
      // Check if for a new create, no value is set, we explicitly set it to UNEXPOSED.
      if (c.userIntent.enableExposingService
          == UniverseDefinitionTaskParams.ExposingServiceState.NONE) {
        c.userIntent.enableExposingService =
            UniverseDefinitionTaskParams.ExposingServiceState.UNEXPOSED;
      }

      if (c.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
        try {
          checkK8sProviderAvailability(provider, customer);
        } catch (IllegalArgumentException e) {
          throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
        }
        checkHelmChartExists(c.userIntent.ybSoftwareVersion);
      }

      // Set the node exporter config based on the provider
      if (!c.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
        AccessKey accessKey = AccessKey.get(provider.uuid, c.userIntent.accessKeyCode);
        AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
        boolean installNodeExporter = keyInfo.installNodeExporter;
        int nodeExporterPort = keyInfo.nodeExporterPort;
        String nodeExporterUser = keyInfo.nodeExporterUser;
        taskParams.extraDependencies.installNodeExporter = installNodeExporter;
        taskParams.communicationPorts.nodeExporterPort = nodeExporterPort;

        for (NodeDetails node : taskParams.nodeDetailsSet) {
          node.nodeExporterPort = nodeExporterPort;
        }

        if (installNodeExporter) {
          taskParams.nodeExporterUser = nodeExporterUser;
        }
      }

      PlacementInfoUtil.updatePlacementInfo(taskParams.getNodesInCluster(c.uuid), c.placementInfo);
    }

    if (taskParams.getPrimaryCluster() != null) {
      UniverseDefinitionTaskParams.UserIntent userIntent =
          taskParams.getPrimaryCluster().userIntent;
      if (userIntent.providerType.isVM() && userIntent.enableYSQL) {
        taskParams.setTxnTableWaitCountFlag = true;
      }
      if (!(userIntent.enableYSQL || userIntent.enableYCQL)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Enable atleast one endpoint among YSQL and YCQL");
      }
      if (!userIntent.enableYSQL && userIntent.enableYSQLAuth) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot enable YSQL Authentication if YSQL endpoint is disabled.");
      }
      if (!userIntent.enableYCQL && userIntent.enableYCQLAuth) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot enable YCQL Authentication if YCQL endpoint is disabled.");
      }
      try {
        if (userIntent.enableYSQLAuth) {
          passwordPolicyService.checkPasswordPolicy(null, userIntent.ysqlPassword);
        }
        if (userIntent.enableYCQLAuth) {
          passwordPolicyService.checkPasswordPolicy(null, userIntent.ycqlPassword);
        }
      } catch (Exception e) {
        throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
      }
      for (Cluster readOnlyCluster : taskParams.getReadOnlyClusters()) {
        validateConsistency(taskParams.getPrimaryCluster(), readOnlyCluster);
      }
    }

    checkGeoPartitioningParameters(customer, taskParams, OpType.CREATE);

    // Create a new universe. This makes sure that a universe of this name does not already exist
    // for this customer id.
    Universe universe = Universe.create(taskParams, customer.getCustomerId());
    LOG.info("Created universe {} : {}.", universe.universeUUID, universe.name);

    // Add an entry for the universe into the customer table.
    customer.addUniverseUUID(universe.universeUUID);
    customer.save();

    LOG.info(
        "Added universe {} : {} for customer [{}].",
        universe.universeUUID,
        universe.name,
        customer.getCustomerId());

    TaskType taskType = TaskType.CreateUniverse;
    Cluster primaryCluster = taskParams.getPrimaryCluster();

    if (primaryCluster != null) {
      UniverseDefinitionTaskParams.UserIntent primaryIntent = primaryCluster.userIntent;
      primaryIntent.masterGFlags = trimFlags(primaryIntent.masterGFlags);
      primaryIntent.tserverGFlags = trimFlags(primaryIntent.tserverGFlags);
      if (primaryCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
        taskType = TaskType.CreateKubernetesUniverse;
        universe.updateConfig(
            ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
      } else {
        if (primaryCluster.userIntent.enableIPV6) {
          throw new PlatformServiceException(
              BAD_REQUEST, "IPV6 not supported for platform deployed VMs.");
        }
      }

      checkForCertificates(customer, taskParams);

      if (primaryCluster.userIntent.enableNodeToNodeEncrypt
          || primaryCluster.userIntent.enableClientToNodeEncrypt) {
        // Set the flag to mark the universe as using TLS enabled and therefore not allowing
        // insecure connections.
        taskParams.allowInsecure = false;
      }

      // TODO: (Daniel) - Move this out to an async task
      if (primaryCluster.userIntent.enableVolumeEncryption
          && primaryCluster.userIntent.providerType.equals(Common.CloudType.aws)) {
        byte[] cmkArnBytes =
            keyManager.generateUniverseKey(
                taskParams.encryptionAtRestConfig.kmsConfigUUID,
                universe.universeUUID,
                taskParams.encryptionAtRestConfig);
        if (cmkArnBytes == null || cmkArnBytes.length == 0) {
          primaryCluster.userIntent.enableVolumeEncryption = false;
        } else {
          // TODO: (Daniel) - Update this to be inside of encryptionAtRestConfig
          taskParams.cmkArn = new String(cmkArnBytes);
        }
      }
    }

    universe.updateConfig(ImmutableMap.of(Universe.TAKE_BACKUPS, "true"));
    // If cloud enabled and deployment AZs have two subnets, mark the cluster as a
    // non legacy cluster for proper operations.
    if (runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled")) {
      Provider provider =
          Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
      AvailabilityZone zone = provider.regions.get(0).zones.get(0);
      if (zone.secondarySubnet != null) {
        universe.updateConfig(ImmutableMap.of(Universe.DUAL_NET_LEGACY, "false"));
      }
    }

    universe.updateConfig(
        ImmutableMap.of(
            Universe.USE_CUSTOM_IMAGE,
            Boolean.toString(taskParams.nodeDetailsSet.stream().allMatch(n -> n.ybPrebuiltAmi))));

    // Submit the task to create the universe.
    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted create universe for {}:{}, task uuid = {}.",
        universe.universeUUID,
        universe.name,
        taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Create,
        universe.name);
    LOG.info(
        "Saved task uuid "
            + taskUUID
            + " in customer tasks table for universe "
            + universe.universeUUID
            + ":"
            + universe.name);

    return UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf());
  }

  /**
   * Update Universe with given params. Updates only one cluster at a time (PRIMARY or Read Replica)
   *
   * @return task UUID of customer task that will actually do the update in the background
   */
  public UUID update(Customer customer, Universe u, UniverseDefinitionTaskParams taskParams) {
    checkCanEdit(customer, u);
    checkTaskParamsForUpdate(u, taskParams);
    if (taskParams.getPrimaryCluster() == null) {
      // Update of a read only cluster.
      return updateCluster(customer, u, taskParams);
    } else {
      return updatePrimaryCluster(customer, u, taskParams);
    }
  }

  private UUID updatePrimaryCluster(
      Customer customer, Universe u, UniverseDefinitionTaskParams taskParams) {

    checkGeoPartitioningParameters(customer, taskParams, OpType.UPDATE);

    // Update Primary cluster
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    for (Cluster readOnlyCluster : u.getUniverseDetails().getReadOnlyClusters()) {
      validateConsistency(primaryCluster, readOnlyCluster);
    }

    TaskType taskType = TaskType.EditUniverse;
    if (primaryCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      taskType = TaskType.EditKubernetesUniverse;
      notHelm2LegacyOrBadRequest(u);
      checkHelmChartExists(primaryCluster.userIntent.ybSoftwareVersion);
    } else {
      mergeNodeExporterInfo(u, taskParams);
    }
    PlacementInfoUtil.updatePlacementInfo(
        taskParams.getNodesInCluster(primaryCluster.uuid), primaryCluster.placementInfo);
    return submitEditUniverse(customer, u, taskParams, taskType, CustomerTask.TargetType.Universe);
  }

  private UUID updateCluster(
      Customer customer, Universe u, UniverseDefinitionTaskParams taskParams) {
    Cluster cluster = getOnlyReadReplicaOrBadRequest(taskParams.getReadOnlyClusters());
    validateConsistency(u.getUniverseDetails().getPrimaryCluster(), cluster);
    PlacementInfoUtil.updatePlacementInfo(
        taskParams.getNodesInCluster(cluster.uuid), cluster.placementInfo);
    return submitEditUniverse(
        customer, u, taskParams, TaskType.EditUniverse, CustomerTask.TargetType.Cluster);
  }

  /** Merge node exporter related information from current universe details to the task params */
  private void mergeNodeExporterInfo(Universe u, UniverseDefinitionTaskParams taskParams) {
    // Set the node exporter config based on the provider
    UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
    boolean installNodeExporter = universeDetails.extraDependencies.installNodeExporter;
    int nodeExporterPort = universeDetails.communicationPorts.nodeExporterPort;
    String nodeExporterUser = universeDetails.nodeExporterUser;
    taskParams.extraDependencies.installNodeExporter = installNodeExporter;
    taskParams.communicationPorts.nodeExporterPort = nodeExporterPort;

    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.nodeExporterPort = nodeExporterPort;
    }

    if (installNodeExporter) {
      taskParams.nodeExporterUser = nodeExporterUser;
    }
  }

  private UUID submitEditUniverse(
      Customer customer,
      Universe u,
      UniverseDefinitionTaskParams taskParams,
      TaskType taskType,
      CustomerTask.TargetType targetType) {
    taskParams.rootCA = checkValidRootCA(u.getUniverseDetails().rootCA);
    LOG.info("Found universe {} : name={} at version={}.", u.universeUUID, u.name, u.version);
    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted {} for {} : {}, task uuid = {}.", taskType, u.universeUUID, u.name, taskUUID);
    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer, u.universeUUID, taskUUID, targetType, CustomerTask.TaskType.Update, u.name);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        u.universeUUID,
        u.name);
    return taskUUID;
  }

  private void notHelm2LegacyOrBadRequest(Universe u) {
    Map<String, String> universeConfig = u.getConfig();
    if (!universeConfig.containsKey(Universe.HELM2_LEGACY)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot perform an edit operation on universe "
              + u.universeUUID
              + " as it is not helm 3 compatible. "
              + "Manually migrate the deployment to helm3 "
              + "and then mark the universe as helm 3 compatible.");
    }
  }

  private UUID checkValidRootCA(UUID rootCA) {
    if (!CertificateInfo.isCertificateValid(rootCA)) {
      String errMsg =
          String.format(
              "The certificate %s needs info. Update the cert and retry.",
              CertificateInfo.get(rootCA).label);
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    return rootCA;
  }

  private void checkCanEdit(Customer customer, Universe u) {
    LOG.info("Update universe {} [ {} ] customer {}.", u.name, u.universeUUID, customer.uuid);
    if (!u.getUniverseDetails().isUniverseEditable()) {
      String errMsg = "Universe UUID " + u.universeUUID + " cannot be edited.";
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    if (u.nodesInTransit()) {
      // TODO 503 - Service Unavailable
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot perform an edit operation on universe "
              + u.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }
  }

  public List<UniverseResp> list(Customer customer) {
    List<UniverseResp> universes = new ArrayList<>();
    // TODO: Restrict the list api json payload, possibly to only include UUID, Name etc
    for (Universe universe : customer.getUniverses()) {
      UniverseResp universePayload =
          UniverseResp.create(universe, null, runtimeConfigFactory.globalRuntimeConf());
      universes.add(universePayload);
    }
    return universes;
  }

  public List<UniverseResp> findByName(Customer customer, String name) {
    return Universe.maybeGetUniverseByName(customer.getCustomerId(), name)
        .map(
            value ->
                Collections.singletonList(
                    UniverseResp.create(value, null, runtimeConfigFactory.globalRuntimeConf())))
        .orElseGet(Collections::emptyList);
  }

  public UUID destroy(
      Customer customer,
      Universe universe,
      boolean isForceDelete,
      boolean isDeleteBackups,
      boolean isDeleteAssociatedCerts) {
    LOG.info(
        "Destroy universe, customer uuid: {}, universe: {} [ {} ] ",
        customer.uuid,
        universe.name,
        universe.universeUUID);

    // Create the Commissioner task to destroy the universe.
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = universe.universeUUID;
    // There is no staleness of a delete request. Perform it even if the universe has changed.
    taskParams.expectedUniverseVersion = -1;
    taskParams.customerUUID = customer.uuid;
    taskParams.isForceDelete = isForceDelete;
    taskParams.isDeleteBackups = isDeleteBackups;
    taskParams.isDeleteAssociatedCerts = isDeleteAssociatedCerts;
    // Submit the task to destroy the universe.
    TaskType taskType = TaskType.DestroyUniverse;
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Cluster primaryCluster = universeDetails.getPrimaryCluster();
    if (primaryCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      taskType = TaskType.DestroyKubernetesUniverse;
    }

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted destroy universe for " + universe.universeUUID + ", task uuid = " + taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Delete,
        universe.name);

    LOG.info(
        "Start destroyUniverse " + universe.universeUUID + " for customer [" + customer.name + "]");
    return taskUUID;
  }

  public UUID createCluster(
      Customer customer, Universe universe, UniverseDefinitionTaskParams taskParams) {
    LOG.info("Create cluster for {} in {}.", customer.uuid, universe.universeUUID);
    // Get the user submitted form data.

    if (taskParams.clusters == null || taskParams.clusters.size() != 1)
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid 'clusters' field/size: "
              + taskParams.clusters
              + " for "
              + universe.universeUUID);

    List<Cluster> newReadOnlyClusters = taskParams.clusters;
    List<Cluster> existingReadOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
    LOG.info(
        "newReadOnly={}, existingRO={}.",
        newReadOnlyClusters.size(),
        existingReadOnlyClusters.size());

    if (existingReadOnlyClusters.size() > 0 && newReadOnlyClusters.size() > 0) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can only have one read-only cluster per universe for now.");
    }

    Cluster cluster = getOnlyReadReplicaOrBadRequest(newReadOnlyClusters);
    if (cluster.uuid == null) {
      String errMsg = "UUID of read-only cluster should be non-null.";
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    if (cluster.clusterType != UniverseDefinitionTaskParams.ClusterType.ASYNC) {
      String errMsg =
          "Read-only cluster type should be "
              + UniverseDefinitionTaskParams.ClusterType.ASYNC
              + " but is "
              + cluster.clusterType;
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    validateConsistency(primaryCluster, cluster);

    // Set the provider code.
    Cluster c = taskParams.clusters.get(0);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(c.userIntent.provider));
    c.userIntent.providerType = Common.CloudType.valueOf(provider.code);
    c.validate();

    if (c.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      try {
        checkK8sProviderAvailability(provider, customer);
      } catch (IllegalArgumentException e) {
        throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
      }
    }

    PlacementInfoUtil.updatePlacementInfo(taskParams.getNodesInCluster(c.uuid), c.placementInfo);

    // Submit the task to create the cluster.
    UUID taskUUID = commissioner.submit(TaskType.ReadOnlyClusterCreate, taskParams);
    LOG.info(
        "Submitted create cluster for {}:{}, task uuid = {}.",
        universe.universeUUID,
        universe.name,
        taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Cluster,
        CustomerTask.TaskType.Create,
        universe.name);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}",
        taskUUID,
        universe.universeUUID,
        universe.name);
    return taskUUID;
  }

  static void validateConsistency(Cluster primaryCluster, Cluster cluster) {
    checkEquals(c -> c.userIntent.enableYSQL, primaryCluster, cluster, "Ysql setting");
    checkEquals(c -> c.userIntent.enableYSQLAuth, primaryCluster, cluster, "Ysql auth setting");
    checkEquals(c -> c.userIntent.enableYCQL, primaryCluster, cluster, "Ycql setting");
    checkEquals(c -> c.userIntent.enableYCQLAuth, primaryCluster, cluster, "Ycql auth setting");
    checkEquals(c -> c.userIntent.enableYEDIS, primaryCluster, cluster, "Yedis setting");
    checkEquals(
        c -> c.userIntent.enableClientToNodeEncrypt,
        primaryCluster,
        cluster,
        "Client to node encrypt setting");
    checkEquals(
        c -> c.userIntent.enableNodeToNodeEncrypt,
        primaryCluster,
        cluster,
        "Node to node encrypt setting");
    checkEquals(
        c -> c.userIntent.assignPublicIP, primaryCluster, cluster, "Assign public IP setting");
  }

  private static void checkEquals(
      Function<Cluster, Object> extractor,
      Cluster primaryCluster,
      Cluster readonlyCluster,
      String errorPrefix) {
    if (!Objects.equals(extractor.apply(primaryCluster), extractor.apply(readonlyCluster))) {
      String error =
          errorPrefix
              + " should be the same for primary and readonly replica "
              + extractor.apply(primaryCluster)
              + " vs "
              + extractor.apply(readonlyCluster);
      LOG.error(error);
      throw new PlatformServiceException(BAD_REQUEST, error);
    }
  }

  public UUID clusterDelete(
      Customer customer, Universe universe, UUID clusterUUID, Boolean isForceDelete) {
    List<Cluster> existingReadOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();

    Cluster cluster = getOnlyReadReplicaOrBadRequest(existingReadOnlyClusters);
    UUID uuid = cluster.uuid;
    if (!uuid.equals(clusterUUID)) {
      String errMsg =
          "Uuid " + clusterUUID + " to delete cluster not found, only " + uuid + " found.";
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Create the Commissioner task to destroy the universe.
    ReadOnlyClusterDelete.Params taskParams = new ReadOnlyClusterDelete.Params();
    taskParams.universeUUID = universe.universeUUID;
    taskParams.clusterUUID = clusterUUID;
    taskParams.isForceDelete = isForceDelete;
    taskParams.expectedUniverseVersion = universe.version;

    // Submit the task to delete the cluster.
    UUID taskUUID = commissioner.submit(TaskType.ReadOnlyClusterDelete, taskParams);
    LOG.info(
        "Submitted delete cluster for {} in {}, task uuid = {}.",
        clusterUUID,
        universe.name,
        taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Cluster,
        CustomerTask.TaskType.Delete,
        universe.name);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}",
        taskUUID,
        universe.universeUUID,
        universe.name);
    return taskUUID;
  }

  private Cluster getOnlyReadReplicaOrBadRequest(List<Cluster> readReplicaClusters) {
    if (readReplicaClusters.size() != 1) {
      String errMsg =
          "Only one read-only cluster expected, but we got " + readReplicaClusters.size();
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    return readReplicaClusters.get(0);
  }

  /**
   * Throw an exception if the given provider has an AZ with KUBENAMESPACE in the config and the
   * provdier has a cluster associated with it. Providers with namespace setting don't support
   * multiple clusters.
   *
   * @param providerToCheck Provider object
   */
  private static void checkK8sProviderAvailability(Provider providerToCheck, Customer customer) {
    boolean isNamespaceSet = false;
    for (Region r : Region.getByProvider(providerToCheck.uuid)) {
      for (AvailabilityZone az : AvailabilityZone.getAZsForRegion(r.uuid)) {
        if (az.getUnmaskedConfig().containsKey("KUBENAMESPACE")) {
          isNamespaceSet = true;
        }
      }
    }

    if (isNamespaceSet) {
      for (UUID universeUUID : Universe.getAllUUIDs(customer)) {
        Universe u = Universe.getOrBadRequest(universeUUID);
        List<Cluster> clusters = u.getUniverseDetails().getReadOnlyClusters();
        clusters.add(u.getUniverseDetails().getPrimaryCluster());
        for (Cluster c : clusters) {
          UUID providerUUID = UUID.fromString(c.userIntent.provider);
          if (providerUUID.equals(providerToCheck.uuid)) {
            String msg =
                "Universe "
                    + u.name
                    + " ("
                    + u.universeUUID
                    + ") already exists with provider "
                    + providerToCheck.name
                    + " ("
                    + providerToCheck.uuid
                    + "). Only one universe can be created with providers having KUBENAMESPACE set "
                    + "in the AZ config.";
            LOG.error(msg);
            throw new IllegalArgumentException(msg);
          }
        }
      }
    }
  }

  public UUID upgrade(Customer customer, Universe universe, UpgradeParams taskParams) {
    if (taskParams.taskType == null) {
      throw new PlatformServiceException(BAD_REQUEST, "task type is required");
    }

    if (taskParams.upgradeOption == UpgradeParams.UpgradeOption.ROLLING_UPGRADE
        && universe.nodesInTransit()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot perform rolling upgrade of universe "
              + universe.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }

    // TODO: we need to refactor this to read from cluster
    // instead of top level task param, for now just copy the master flag and tserver flag
    // from primary cluster.
    UniverseDefinitionTaskParams.UserIntent primaryIntent =
        taskParams.getPrimaryCluster().userIntent;
    primaryIntent.masterGFlags = trimFlags(primaryIntent.masterGFlags);
    primaryIntent.tserverGFlags = trimFlags(primaryIntent.tserverGFlags);
    taskParams.masterGFlags = primaryIntent.masterGFlags;
    taskParams.tserverGFlags = primaryIntent.tserverGFlags;

    CustomerTask.TaskType customerTaskType;
    // Validate if any required params are missed based on the taskType
    switch (taskParams.taskType) {
      case VMImage:
        if (!runtimeConfigFactory.forUniverse(universe).getBoolean("yb.cloud.enabled")) {
          throw new PlatformServiceException(
              Http.Status.METHOD_NOT_ALLOWED, "VM image upgrade is disabled");
        }

        Common.CloudType provider = primaryIntent.providerType;
        if (!(provider == Common.CloudType.gcp || provider == Common.CloudType.aws)) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "VM image upgrade is only supported for AWS / GCP, got: " + provider.toString());
        }

        if (UniverseDefinitionTaskParams.hasEphemeralStorage(primaryIntent)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot upgrade a universe with ephemeral storage");
        }

        if (taskParams.machineImages.isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST, "machineImages param is required for taskType: " + taskParams.taskType);
        }

        customerTaskType = CustomerTask.TaskType.UpgradeVMImage;
        break;
      case ResizeNode:
        Common.CloudType providerType =
            universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
        if (!(providerType.equals(Common.CloudType.gcp)
            || providerType.equals(Common.CloudType.aws))) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Smart resizing is only supported for AWS / GCP, It is: " + providerType.toString());
        }

        customerTaskType = CustomerTask.TaskType.ResizeNode;
        break;
      case Software:
        customerTaskType = CustomerTask.TaskType.UpgradeSoftware;
        if (taskParams.ybSoftwareVersion == null || taskParams.ybSoftwareVersion.isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "ybSoftwareVersion param is required for taskType: " + taskParams.taskType);
        }
        UniverseDefinitionTaskParams.UserIntent uIntent =
            universe.getUniverseDetails().getPrimaryCluster().userIntent;
        taskParams.ybPrevSoftwareVersion = uIntent.ybSoftwareVersion;
        break;
      case GFlags:
        customerTaskType = CustomerTask.TaskType.UpgradeGflags;
        // TODO(BUG): This looks like a bug. This should check for empty instead of null.
        // Fixing this cause unit test to break. Leaving the TODO for now.
        if (taskParams.masterGFlags == null && taskParams.tserverGFlags == null) {
          throw new PlatformServiceException(
              BAD_REQUEST, "gflags param is required for taskType: " + taskParams.taskType);
        }
        UniverseDefinitionTaskParams.UserIntent univIntent =
            universe.getUniverseDetails().getPrimaryCluster().userIntent;
        if (taskParams.masterGFlags != null
            && taskParams.masterGFlags.equals(univIntent.masterGFlags)
            && taskParams.tserverGFlags != null
            && taskParams.tserverGFlags.equals(univIntent.tserverGFlags)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Neither master nor tserver gflags changed.");
        }
        break;
      case Restart:
        customerTaskType = CustomerTask.TaskType.Restart;
        if (taskParams.upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Rolling restart has to be a ROLLING UPGRADE.");
        }
        break;
      case Systemd:
        customerTaskType = CustomerTask.TaskType.SystemdUpgrade;
        if (taskParams.upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Rolling restart has to be a ROLLING UPGRADE.");
        }
        break;
      case Certs:
        customerTaskType = CustomerTask.TaskType.UpdateCert;
        if (taskParams.certUUID == null) {
          throw new PlatformServiceException(
              BAD_REQUEST, "certUUID is required for taskType: " + taskParams.taskType);
        }
        if (!taskParams
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(Common.CloudType.onprem)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Certs can only be rotated for onprem." + taskParams.taskType);
        }
        CertificateInfo cert = CertificateInfo.get(taskParams.certUUID);
        if (cert.certType != CertConfigType.CustomCertHostPath) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Need a custom cert. Cannot use self-signed." + taskParams.taskType);
        }
        cert = CertificateInfo.get(universe.getUniverseDetails().rootCA);
        if (cert.certType != CertConfigType.CustomCertHostPath) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Only custom certs can be rotated." + taskParams.taskType);
        }
        break;
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unexpected value: " + taskParams.taskType);
    }

    LOG.info("Got task type {}", customerTaskType.toString());
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = universe.version;

    LOG.info(
        "Found universe {} : name={} at version={}.",
        universe.universeUUID,
        universe.name,
        universe.version);

    Map<String, String> universeConfig = universe.getConfig();
    TaskType taskType = TaskType.UpgradeUniverse;
    if (taskParams
        .getPrimaryCluster()
        .userIntent
        .providerType
        .equals(Common.CloudType.kubernetes)) {
      taskType = TaskType.UpgradeKubernetesUniverse;
      if (!universeConfig.containsKey(Universe.HELM2_LEGACY)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Cannot perform upgrade operation on universe. "
                + universe.universeUUID
                + " as it is not helm 3 compatible. "
                + "Manually migrate the deployment to helm3 "
                + "and then mark the universe as helm 3 compatible.");
      }

      if (customerTaskType == CustomerTask.TaskType.UpgradeGflags) {
        // UpgradeGflags does not change universe version. Check for current version of helm chart.
        checkHelmChartExists(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      } else {
        checkHelmChartExists(taskParams.ybSoftwareVersion);
      }
    }

    taskParams.rootCA = checkValidRootCA(universe.getUniverseDetails().rootCA);

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted upgrade universe for {} : {}, task uuid = {}.",
        universe.universeUUID,
        universe.name,
        taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        customerTaskType,
        universe.name);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.universeUUID,
        universe.name);
    return taskUUID;
  }

  public UUID updateDiskSize(
      Customer customer, Universe universe, DiskIncreaseFormData taskParams) {
    LOG.info("Disk Size Increase {} for {}.", customer.uuid, universe.universeUUID);
    if (taskParams.size == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Size cannot be 0.");
    }
    UniverseDefinitionTaskParams.UserIntent primaryIntent =
        taskParams.getPrimaryCluster().userIntent;
    if (taskParams.size <= primaryIntent.deviceInfo.volumeSize) {
      throw new PlatformServiceException(BAD_REQUEST, "Size can only be increased.");
    }
    if (UniverseDefinitionTaskParams.hasEphemeralStorage(primaryIntent)) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot modify instance volumes.");
    }

    primaryIntent.deviceInfo.volumeSize = taskParams.size;
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = universe.version;
    LOG.info(
        "Found universe {} : name={} at version={}.",
        universe.universeUUID,
        universe.name,
        universe.version);

    TaskType taskType = TaskType.UpdateDiskSize;
    if (taskParams
        .getPrimaryCluster()
        .userIntent
        .providerType
        .equals(Common.CloudType.kubernetes)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Kubernetes disk size increase not yet supported.");
    }

    UUID taskUUID = commissioner.submit(taskType, taskParams);
    LOG.info(
        "Submitted update disk universe for {} : {}, task uuid = {}.",
        universe.universeUUID,
        universe.name,
        taskUUID);

    CustomerTask.TaskType customerTaskType = CustomerTask.TaskType.UpdateDiskSize;

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        customerTaskType,
        universe.name);
    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {} : {}.",
        taskUUID,
        universe.universeUUID,
        universe.name);
    return taskUUID;
  }

  public UUID tlsConfigUpdate(
      Customer customer, Universe universe, TlsConfigUpdateParams taskParams) {

    LOG.info("tlsConfigUpdate: {}", Json.toJson(CommonUtils.maskObject(taskParams)));

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;

    if (taskParams.rootAndClientRootCASame == null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "rootAndClientRootCASame cannot be null.");
    }

    boolean nodeToNodeChange =
        taskParams.enableNodeToNodeEncrypt != null
            && taskParams.enableNodeToNodeEncrypt != userIntent.enableNodeToNodeEncrypt;
    boolean clientToNodeChange =
        taskParams.enableClientToNodeEncrypt != null
            && taskParams.enableClientToNodeEncrypt != userIntent.enableClientToNodeEncrypt;
    boolean tlsToggle = (nodeToNodeChange || clientToNodeChange);

    boolean rootCaChange =
        taskParams.rootCA != null && !taskParams.rootCA.equals(universeDetails.rootCA);
    boolean clientRootCaChange =
        !taskParams.rootAndClientRootCASame
            && taskParams.clientRootCA != null
            && !taskParams.clientRootCA.equals(universeDetails.clientRootCA);
    boolean certsRotate =
        rootCaChange
            || clientRootCaChange
            || taskParams.createNewRootCA
            || taskParams.createNewClientRootCA
            || taskParams.selfSignedServerCertRotate
            || taskParams.selfSignedClientCertRotate;

    if (tlsToggle && certsRotate) {
      if (((rootCaChange || taskParams.createNewRootCA) && universeDetails.rootCA != null)
          || ((clientRootCaChange || taskParams.createNewClientRootCA)
              && universeDetails.clientRootCA != null)) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "Cannot enable/disable TLS along with cert rotation. Perform them individually.");
      } else {
        certsRotate = false;
      }
    }

    if (!tlsToggle && !certsRotate) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "No changes in Tls parameters, cannot perform upgrade.");
    }

    if (certsRotate
        && ((rootCaChange && taskParams.selfSignedServerCertRotate)
            || (clientRootCaChange && taskParams.selfSignedClientCertRotate))) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Cannot update rootCA/clientRootCA when "
              + "selfSignedServerCertRotate/selfSignedClientCertRotate is set to true");
    }

    if (certsRotate
        && ((taskParams.createNewRootCA && taskParams.selfSignedServerCertRotate)
            || (taskParams.createNewClientRootCA && taskParams.selfSignedClientCertRotate))) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "Cannot create new rootCA/clientRootCA when "
              + "selfSignedServerCertRotate/selfSignedClientCertRotate is set to true");
    }

    if (tlsToggle) {
      boolean isRootCA =
          EncryptionInTransitUtil.isRootCARequired(
              taskParams.enableNodeToNodeEncrypt,
              taskParams.enableClientToNodeEncrypt,
              taskParams.rootAndClientRootCASame);
      boolean isClientRootCA =
          EncryptionInTransitUtil.isClientRootCARequired(
              taskParams.enableNodeToNodeEncrypt,
              taskParams.enableClientToNodeEncrypt,
              taskParams.rootAndClientRootCASame);

      TlsToggleParams tlsToggleParams = new TlsToggleParams();
      tlsToggleParams.enableNodeToNodeEncrypt = taskParams.enableNodeToNodeEncrypt;
      tlsToggleParams.enableClientToNodeEncrypt = taskParams.enableClientToNodeEncrypt;
      tlsToggleParams.allowInsecure =
          !(taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt);
      tlsToggleParams.rootCA =
          isRootCA ? (!taskParams.createNewRootCA ? taskParams.rootCA : null) : null;
      tlsToggleParams.clientRootCA =
          isClientRootCA
              ? (!taskParams.createNewClientRootCA ? taskParams.clientRootCA : null)
              : null;
      tlsToggleParams.rootAndClientRootCASame = taskParams.rootAndClientRootCASame;
      tlsToggleParams.upgradeOption = taskParams.upgradeOption;
      tlsToggleParams.sleepAfterMasterRestartMillis = taskParams.sleepAfterMasterRestartMillis;
      tlsToggleParams.sleepAfterTServerRestartMillis = taskParams.sleepAfterTServerRestartMillis;
      return upgradeUniverseHandler.toggleTls(tlsToggleParams, customer, universe);
    }

    if (certsRotate) {
      boolean isRootCA =
          EncryptionInTransitUtil.isRootCARequired(
              userIntent.enableNodeToNodeEncrypt,
              userIntent.enableClientToNodeEncrypt,
              taskParams.rootAndClientRootCASame);
      boolean isClientRootCA =
          EncryptionInTransitUtil.isClientRootCARequired(
              userIntent.enableNodeToNodeEncrypt,
              userIntent.enableClientToNodeEncrypt,
              taskParams.rootAndClientRootCASame);

      if (isRootCA && taskParams.createNewRootCA) {
        taskParams.rootCA =
            CertificateHelper.createRootCA(
                universeDetails.nodePrefix,
                customer.uuid,
                runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"));
      }

      if (isClientRootCA && taskParams.createNewClientRootCA) {
        taskParams.clientRootCA =
            CertificateHelper.createClientRootCA(
                universeDetails.nodePrefix,
                customer.uuid,
                runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path"));
      }

      CertsRotateParams certsRotateParams =
          CertsRotateParams.mergeUniverseDetails(taskParams, universe.getUniverseDetails());

      LOG.info("CertsRotateParams : {}", Json.toJson(CommonUtils.maskObject(certsRotateParams)));

      return upgradeUniverseHandler.rotateCerts(certsRotateParams, customer, universe);
    }

    return null;
  }

  private void checkHelmChartExists(String ybSoftwareVersion) {
    try {
      kubernetesManagerFactory.getManager().getHelmPackagePath(ybSoftwareVersion);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
  }

  // This compares and validates an input node with the existing node that is not in ToBeAddedState.
  private void checkNodesForUpdate(
      Cluster cluster, NodeDetails existingNode, NodeDetails inputNode) {
    if (inputNode.cloudInfo == null) {
      String errMsg = String.format("Node name %s must have cloudInfo", inputNode.nodeName);
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    // TODO compare other node fields here.
  }

  // This validates the input nodes in a cluster by comparing with the existing nodes in the
  // universe.
  private void checkNodesInClusterForUpdate(
      Cluster cluster, Set<NodeDetails> existingNodes, Set<NodeDetails> inputNodes) {
    AtomicInteger inputNodesInToBeRemoved = new AtomicInteger();
    // Collect all the nodes which are not in ToBeAdded state and validate.
    Map<String, NodeDetails> inputNodesMap =
        inputNodes
            .stream()
            .filter(
                node -> {
                  if (node.state != NodeState.ToBeAdded) {
                    if (node.state == NodeState.ToBeRemoved) {
                      inputNodesInToBeRemoved.incrementAndGet();
                    }
                    return true;
                  }
                  // Nodes in ToBeAdded must not have names.
                  if (StringUtils.isNotBlank(node.nodeName)) {
                    String errMsg = String.format("Node name %s cannot be present", node.nodeName);
                    LOG.error(errMsg);
                    throw new PlatformServiceException(BAD_REQUEST, errMsg);
                  }
                  return false;
                })
            .collect(
                Collectors.toMap(
                    NodeDetails::getNodeName,
                    Function.identity(),
                    (existing, replacement) -> {
                      String errMsg = "Duplicate node name " + existing;
                      LOG.error(errMsg);
                      throw new PlatformServiceException(BAD_REQUEST, errMsg);
                    }));

    // Ensure all the input nodes for a cluster in the input are not set to ToBeRemoved.
    // If some nodes are in other state, the count will always be smaller.
    if (inputNodes.size() > 0 && inputNodesInToBeRemoved.get() == inputNodes.size()) {
      String errMsg = "All nodes cannot be removed for cluster " + cluster.uuid;
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Ensure all the nodes in the cluster are present in the input.
    existingNodes
        .stream()
        .filter(existingNode -> existingNode.state != NodeState.ToBeAdded)
        .forEach(
            existingNode -> {
              NodeDetails inputNode = inputNodesMap.remove(existingNode.getNodeName());
              if (inputNode == null) {
                String errMsg = String.format("Node %s is missing", existingNode.getNodeName());
                LOG.error(errMsg);
                throw new PlatformServiceException(BAD_REQUEST, errMsg);
              }
              checkNodesForUpdate(cluster, existingNode, inputNode);
            });

    // Ensure unknown nodes are in the input.
    if (!inputNodesMap.isEmpty()) {
      String errMsg = "Unknown nodes " + StringUtils.join(inputNodesMap.keySet(), ",");
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }

  // TODO This is used by calls originating from the UI that are not in swagger APIs. More
  // validations may be needed later like checking the fields of all NodeDetails objects because
  // the existing nodes in the universe are replaced by these nodes in the task params. UI sends
  // all the nodes irrespective of the cluster. Only the cluster getting updated, is sent in the
  // request.
  private void checkTaskParamsForUpdate(
      Universe universe, UniverseDefinitionTaskParams taskParams) {

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    Set<UUID> taskParamClustersUuids =
        taskParams
            .clusters
            .stream()
            .map(c -> c.uuid)
            .collect(Collectors.toCollection(HashSet::new));

    universeDetails
        .clusters
        .stream()
        .forEach(
            c -> {
              taskParamClustersUuids.remove(c.uuid);
              checkNodesInClusterForUpdate(
                  c,
                  universeDetails.getNodesInCluster(c.uuid),
                  taskParams.getNodesInCluster(c.uuid));
            });

    if (!taskParamClustersUuids.isEmpty()) {
      // Unknown clusters are found. There can be fewer clusters in the input but all those clusters
      // must already be in the universe.
      String errMsg = "Unknown cluster " + StringUtils.join(taskParamClustersUuids, ",");
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }
}
