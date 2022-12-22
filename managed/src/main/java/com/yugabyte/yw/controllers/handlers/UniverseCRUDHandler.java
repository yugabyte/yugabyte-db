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

import com.fasterxml.jackson.core.JsonProcessingException;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.AddOnClusterDelete;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyKubernetesClusterDelete;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.certmgmt.providers.VaultPKI;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.DiskIncreaseFormData;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.TlsConfigUpdateParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Ebean;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.Status;

import javax.annotation.Nullable;
import java.util.ArrayList;
import java.util.Arrays;
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

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

public class UniverseCRUDHandler {

  private static final Logger LOG = LoggerFactory.getLogger(UniverseCRUDHandler.class);

  @Inject Commissioner commissioner;

  @Inject EncryptionAtRestManager keyManager;

  @Inject play.Configuration appConfig;

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  @Inject SettableRuntimeConfigFactory settableRuntimeConfigFactory;

  @Inject KubernetesManagerFactory kubernetesManagerFactory;
  @Inject PasswordPolicyService passwordPolicyService;

  @Inject UpgradeUniverseHandler upgradeUniverseHandler;

  @Inject YbcManager ybcManager;

  private enum OpType {
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

  public Set<UniverseDefinitionTaskParams.UpdateOptions> getUpdateOptions(
      Customer customer, UniverseConfigureTaskParams taskParams) {
    Cluster cluster =
        taskParams.getCurrentClusterType().equals(UniverseDefinitionTaskParams.ClusterType.PRIMARY)
            ? taskParams.getPrimaryCluster()
            : taskParams.getReadOnlyClusters().get(0);
    return getUpdateOptions(
        taskParams, cluster, PlacementInfoUtil.getUniverseForParams(taskParams));
  }

  private Set<UniverseDefinitionTaskParams.UpdateOptions> getUpdateOptions(
      UniverseConfigureTaskParams taskParams, Cluster cluster, @Nullable Universe universe) {
    if (taskParams.clusterOperation == UniverseConfigureTaskParams.ClusterOperationType.CREATE
        || universe == null) {
      return Collections.singleton(UniverseDefinitionTaskParams.UpdateOptions.UPDATE);
    }
    Cluster currentCluster = universe.getCluster(cluster.uuid);
    Set<UniverseDefinitionTaskParams.UpdateOptions> result = new HashSet<>();
    Set<NodeDetails> nodesInCluster = taskParams.getNodesInCluster(cluster.uuid);
    boolean hasChangedNodes = false;
    boolean hasRemainingNodes = false;

    boolean smartResizePossible =
        ResizeNodeParams.checkResizeIsPossible(
            currentCluster.userIntent, cluster.userIntent, universe, true);

    for (NodeDetails node : nodesInCluster) {
      if (node.state == NodeState.ToBeAdded || node.state == NodeState.ToBeRemoved) {
        hasChangedNodes = true;
      } else {
        hasRemainingNodes = true;
      }
    }
    if (!hasRemainingNodes) {
      result.add(UniverseDefinitionTaskParams.UpdateOptions.FULL_MOVE);
      if (!PlacementInfoUtil.isSamePlacement(currentCluster.placementInfo, cluster.placementInfo)) {
        smartResizePossible = false;
      }
    } else {
      if (hasChangedNodes || !cluster.areTagsSame(currentCluster)) {
        result.add(UniverseDefinitionTaskParams.UpdateOptions.UPDATE);
      } else if (GFlagsUtil.checkGFlagsByIntentChange(
          currentCluster.userIntent, cluster.userIntent)) {
        result.add(UniverseDefinitionTaskParams.UpdateOptions.GFLAGS_UPGRADE);
      }
    }
    if (smartResizePossible
        && (result.isEmpty()
            || result.equals(
                Collections.singleton(UniverseDefinitionTaskParams.UpdateOptions.FULL_MOVE)))) {
      if (cluster.userIntent.instanceType == null
          || cluster.userIntent.instanceType.equals(currentCluster.userIntent.instanceType)) {
        result.add(UniverseDefinitionTaskParams.UpdateOptions.SMART_RESIZE_NON_RESTART);
      } else {
        result.add(UniverseDefinitionTaskParams.UpdateOptions.SMART_RESIZE);
      }
    }
    return result;
  }

  private Cluster getClusterFromTaskParams(UniverseConfigureTaskParams taskParams) {
    Cluster cluster;
    if (taskParams.currentClusterType.equals(UniverseDefinitionTaskParams.ClusterType.PRIMARY)) {
      cluster = taskParams.getPrimaryCluster();
    } else if (taskParams.currentClusterType.equals(
        UniverseDefinitionTaskParams.ClusterType.ASYNC)) {
      cluster = taskParams.getReadOnlyClusters().get(0);
    } else if (taskParams.currentClusterType.equals(
        UniverseDefinitionTaskParams.ClusterType.ADDON)) {
      cluster = taskParams.getAddOnClusters().get(0);
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid cluster type");
    }

    return cluster;
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
    Cluster cluster = getClusterFromTaskParams(taskParams);
    UniverseDefinitionTaskParams.UserIntent primaryIntent = cluster.userIntent;

    checkGeoPartitioningParameters(customer, taskParams, OpType.CONFIGURE);

    primaryIntent.masterGFlags = trimFlags(primaryIntent.masterGFlags);
    primaryIntent.tserverGFlags = trimFlags(primaryIntent.tserverGFlags);
    if (StringUtils.isEmpty(primaryIntent.accessKeyCode)) {
      primaryIntent.accessKeyCode = appConfig.getString("yb.security.default.access.key");
    }
    try {
      Universe universe = PlacementInfoUtil.getUniverseForParams(taskParams);
      PlacementInfoUtil.updateUniverseDefinition(
          taskParams, universe, customer.getCustomerId(), cluster.uuid);
      try {
        if (taskParams
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(Common.CloudType.kubernetes)) {
          taskParams.updateOptions =
              Collections.singleton(
                  UniverseDefinitionTaskParams.UpdateOptions.SMART_RESIZE_NON_RESTART);
        } else {
          taskParams.updateOptions = getUpdateOptions(taskParams, cluster, universe);
        }
      } catch (Exception e) {
        LOG.error("Failed to calculate update options", e);
      }
      UniverseResp.fillClusterRegions(taskParams.clusters);
    } catch (IllegalStateException | IllegalArgumentException | UnsupportedOperationException e) {
      LOG.error("Failed to update universe definition", e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
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
          if (taskParams.mastersInDefaultRegion) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Could not pick %d masters, only %d nodes available in default region %s.",
                    intent.replicationFactor, nodesInDefRegion, defaultRegion.name));
          } else {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Could not pick %d nodes in default region %s to place enough data replicas.",
                    intent.replicationFactor, defaultRegion.name));
          }
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
                    "Error while dumping certs from Vault for certificate: %s", taskParams.rootCA));
          }
        }
      } else {
        // create self-signed rootCA in case it is not provided by the user.
        taskParams.rootCA =
            CertificateHelper.createRootCA(
                runtimeConfigFactory.staticApplicationConf(), taskParams.nodePrefix, customer.uuid);
      }
      checkValidRootCA(taskParams.rootCA);
    }

    if (primaryCluster.userIntent.enableClientToNodeEncrypt) {
      if (taskParams.getClientRootCA() == null) {
        if (taskParams.rootCA != null && taskParams.rootAndClientRootCASame) {
          // Setting ClientRootCA to RootCA in case rootAndClientRootCA is true
          taskParams.setClientRootCA(taskParams.rootCA);
        } else {
          // create self-signed clientRootCA in case it is not provided by the user
          // and root and clientRoot CA needs to be different
          taskParams.setClientRootCA(
              CertificateHelper.createClientRootCA(
                  runtimeConfigFactory.staticApplicationConf(),
                  taskParams.nodePrefix,
                  customer.uuid));
        }
      }

      cert = CertificateInfo.get(taskParams.getClientRootCA());
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
                  "Error while dumping certs from Vault for certificate: %s",
                  taskParams.getClientRootCA()));
        }
      }

      checkValidRootCA(taskParams.getClientRootCA());

      // Setting rootCA to ClientRootCA in case node to node encryption is disabled.
      // This is necessary to set to ensure backward compatibility as existing parts of
      // codebase (kubernetes) uses rootCA for Client to Node Encryption
      if (taskParams.rootCA == null && taskParams.rootAndClientRootCASame) {
        taskParams.rootCA = taskParams.getClientRootCA();
      }

      // Generate client certs if rootAndClientRootCASame is true and rootCA is self-signed.
      // This is there only for legacy support, no need if rootCA and clientRootCA are different.
      if (taskParams.rootAndClientRootCASame) {
        CertificateInfo rootCert = CertificateInfo.get(taskParams.rootCA);
        if (rootCert.certType == CertConfigType.SelfSigned
            || rootCert.certType == CertConfigType.HashicorpVault) {
          CertificateHelper.createClientCertificate(
              runtimeConfigFactory.staticApplicationConf(), customer.uuid, taskParams.rootCA);
        }
      }
    }
  }

  public void setUpXClusterSettings(UniverseDefinitionTaskParams taskParams) {
    taskParams.xClusterInfo.sourceRootCertDirPath =
        XClusterConfigTaskBase.getProducerCertsDir(
            taskParams.getPrimaryCluster().userIntent.provider);
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
    boolean cloudEnabled =
        runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled");

    for (Cluster c : taskParams.clusters) {
      Provider provider = Provider.getOrBadRequest(UUID.fromString(c.userIntent.provider));
      // Set the provider code.
      c.userIntent.providerType = Common.CloudType.valueOf(provider.code);
      c.validate(!cloudEnabled);
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
        // TODO(bhavin192): there should be some validation on the
        // universe_name when creating the universe, because we cannot
        // have more than 43 characters for universe_name + az_name
        // when using new naming style. The length of longest AZ name
        // should be picked up for that provider and then checked
        // against the give universe_name.
      }

      // Set the node exporter config based on the provider
      if (!c.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
        AccessKey accessKey = AccessKey.get(provider.uuid, c.userIntent.accessKeyCode);
        ProviderDetails providerDetails = provider.details;
        boolean installNodeExporter = providerDetails.installNodeExporter;
        String nodeExporterUser = providerDetails.nodeExporterUser;
        taskParams.extraDependencies.installNodeExporter = installNodeExporter;

        if (c.userIntent.providerType.equals(Common.CloudType.onprem)) {
          int nodeExporterPort = providerDetails.nodeExporterPort;
          taskParams.communicationPorts.nodeExporterPort = nodeExporterPort;

          for (NodeDetails node : taskParams.nodeDetailsSet) {
            node.nodeExporterPort = nodeExporterPort;
          }
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

      if (taskParams.enableYbc) {
        if (Util.compareYbVersions(
                userIntent.ybSoftwareVersion, Util.YBC_COMPATIBLE_DB_VERSION, true)
            < 0) {
          taskParams.enableYbc = false;
          LOG.error(
              "Ybc installation is skipped on universe with DB version lower than "
                  + Util.YBC_COMPATIBLE_DB_VERSION);
        } else {
          taskParams.ybcSoftwareVersion =
              StringUtils.isNotBlank(taskParams.ybcSoftwareVersion)
                  ? taskParams.ybcSoftwareVersion
                  : ybcManager.getStableYbcVersion();
        }
      }

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
    Universe universe;
    TaskType taskType = TaskType.CreateUniverse;

    Ebean.beginTransaction();
    try {
      universe = Universe.create(taskParams, customer.getCustomerId());
      LOG.info("Created universe {} : {}.", universe.universeUUID, universe.name);
      if (taskParams.runtimeFlags != null) {
        // iterate through the flags and set via runtime config
        for (Map.Entry<String, String> entry : taskParams.runtimeFlags.entrySet()) {
          if (entry.getValue() != null) {
            settableRuntimeConfigFactory
                .forUniverse(universe)
                .setValue(entry.getKey(), entry.getValue());
          }
        }
      }

      Cluster primaryCluster = taskParams.getPrimaryCluster();

      if (primaryCluster != null) {
        UniverseDefinitionTaskParams.UserIntent primaryIntent = primaryCluster.userIntent;
        primaryIntent.masterGFlags = trimFlags(primaryIntent.masterGFlags);
        primaryIntent.tserverGFlags = trimFlags(primaryIntent.tserverGFlags);

        if (primaryCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
          taskType = TaskType.CreateKubernetesUniverse;
          universe.updateConfig(
              ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
          universe.save();
          // This flag will be used for testing purposes as well. Don't remove.
          if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.use_new_helm_naming")) {
            if (Util.compareYbVersions(primaryIntent.ybSoftwareVersion, "2.15.4.0") >= 0) {
              taskParams.useNewHelmNamingStyle = true;
            } else {
              if (taskParams.useNewHelmNamingStyle) {
                throw new PlatformServiceException(
                    BAD_REQUEST, "New naming style is not supported for versions < 2.15.4.0");
              }
            }
          }
        } else {
          if (primaryCluster.userIntent.enableIPV6) {
            throw new PlatformServiceException(
                BAD_REQUEST, "IPV6 not supported for platform deployed VMs.");
          }
        }

        setUpXClusterSettings(taskParams);

        checkForCertificates(customer, taskParams);

        if (primaryCluster.userIntent.enableNodeToNodeEncrypt
            || primaryCluster.userIntent.enableClientToNodeEncrypt) {
          // Set the flag to mark the universe as using TLS enabled and therefore not
          // allowing insecure connections.
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
        if (Universe.shouldEnableHttpsUI(
            primaryIntent.enableNodeToNodeEncrypt, primaryIntent.ybSoftwareVersion)) {
          universe.updateConfig(ImmutableMap.of(Universe.HTTPS_ENABLED_UI, "true"));
        }
      }

      // other configs enabled by default
      universe.updateConfig(
          ImmutableMap.of(
              Universe.TAKE_BACKUPS, "true",
              Universe.KEY_CERT_HOT_RELOADABLE, "true"));
      universe.save();
      // If cloud enabled and deployment AZs have two subnets, mark the cluster as a
      // non legacy cluster for proper operations.
      if (cloudEnabled) {
        Provider provider =
            Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
        AvailabilityZone zone = provider.regions.get(0).zones.get(0);
        if (zone.secondarySubnet != null) {
          universe.updateConfig(ImmutableMap.of(Universe.DUAL_NET_LEGACY, "false"));
          universe.save();
        }
      }

      universe.updateConfig(
          ImmutableMap.of(
              Universe.USE_CUSTOM_IMAGE,
              Boolean.toString(taskParams.nodeDetailsSet.stream().allMatch(n -> n.ybPrebuiltAmi))));
      universe.save();

      Ebean.commitTransaction();

    } catch (Exception e) {
      LOG.info("Universe wasn't created because of the error: {}", e.getMessage());
      throw e;
    } finally {
      Ebean.endTransaction();
    }

    // Submit the task to create the universe.

    UUID taskUUID;
    try {
      taskUUID = commissioner.submit(taskType, taskParams);
    } catch (RuntimeException e) {
      // No need to keep this Universe
      Universe.delete(universe.getUniverseUUID());
      throw e;
    }
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
    if (u.isYbcEnabled()) {
      taskParams.installYbc = true;
      taskParams.enableYbc = true;
      taskParams.ybcSoftwareVersion = u.getUniverseDetails().ybcSoftwareVersion;
      taskParams.ybcInstalled = true;
    }

    // Set existing LBs into taskParams
    taskParams.setExistingLBs(u.getUniverseDetails().clusters);

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
    TaskType taskType = TaskType.EditUniverse;
    if (cluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      taskType = TaskType.EditKubernetesUniverse;
      notHelm2LegacyOrBadRequest(u);
      checkHelmChartExists(cluster.userIntent.ybSoftwareVersion);
    }
    return submitEditUniverse(customer, u, taskParams, taskType, CustomerTask.TargetType.Cluster);
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

    // Update all current tasks for this universe to be marked as done if it is a force delete.
    if (isForceDelete) {
      markAllUniverseTasksAsCompleted(universe.universeUUID);
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

    if (taskParams.clusters == null || taskParams.clusters.size() != 1) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid 'clusters' field/size: "
              + taskParams.clusters
              + " for "
              + universe.universeUUID);
    }

    if (universe.isYbcEnabled()) {
      taskParams.installYbc = true;
      taskParams.enableYbc = true;
      taskParams.ybcSoftwareVersion = universe.getUniverseDetails().ybcSoftwareVersion;
      taskParams.ybcInstalled = true;
    }

    List<Cluster> newReadOnlyClusters = taskParams.getReadOnlyClusters();
    List<Cluster> newAddOnClusters = taskParams.getAddOnClusters();
    List<Cluster> existingReadOnlyClusters = universe.getUniverseDetails().getReadOnlyClusters();
    LOG.info(
        "newReadOnly={}, existingReadOnly={}, newAddOn={}",
        newReadOnlyClusters.size(),
        existingReadOnlyClusters.size(),
        newAddOnClusters.size());

    if (newReadOnlyClusters.size() > 0 && newAddOnClusters.size() > 0) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot create both read-only and add-on clusters at the same time.");
    }

    if (newReadOnlyClusters.size() > 0) {
      return createReadReplicaCluster(
          customer, universe, taskParams, existingReadOnlyClusters, newReadOnlyClusters);
    } else if (newAddOnClusters.size() > 0) {
      return createAddOnCluster(customer, universe, taskParams, newAddOnClusters);
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Unknown cluster type specified");
    }
  }

  public UUID createAddOnCluster(
      Customer customer,
      Universe universe,
      UniverseDefinitionTaskParams taskParams,
      List<Cluster> newAddOnClusters) {

    if (newAddOnClusters.size() > 1) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot create more than one add-on cluster at a time.");
    }

    Cluster addOnCluster = newAddOnClusters.get(0);
    if (addOnCluster.uuid == null) {
      String errMsg = "UUID of add-on cluster should be non-null.";
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    if (addOnCluster.clusterType != ClusterType.ADDON) {
      String errMsg =
          "AddOn cluster type should be "
              + ClusterType.ADDON
              + " but is "
              + addOnCluster.clusterType;
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    taskParams.clusters.add(primaryCluster);

    // Set the provider code.
    Provider provider = Provider.getOrBadRequest(UUID.fromString(addOnCluster.userIntent.provider));
    addOnCluster.userIntent.providerType = Common.CloudType.valueOf(provider.code);
    addOnCluster.validate(
        !runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled"));

    TaskType taskType = TaskType.AddOnClusterCreate;
    if (addOnCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      // TODO: Do we need to support this?
      throw new PlatformServiceException(
          BAD_REQUEST, "Kubernetes provider is not supported for add-on clusters.");
    }

    // TODO: do we need this?
    PlacementInfoUtil.updatePlacementInfo(
        taskParams.getNodesInCluster(addOnCluster.uuid), addOnCluster.placementInfo);

    // Submit the task to create the cluster.
    UUID taskUUID = commissioner.submit(taskType, taskParams);
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

  public UUID createReadReplicaCluster(
      Customer customer,
      Universe universe,
      UniverseDefinitionTaskParams taskParams,
      List<Cluster> existingReadOnlyClusters,
      List<Cluster> newReadOnlyClusters) {

    if (existingReadOnlyClusters.size() > 0 && newReadOnlyClusters.size() > 0) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can only have one read-only cluster per universe for now.");
    }

    Cluster readOnlyCluster = getOnlyReadReplicaOrBadRequest(newReadOnlyClusters);
    if (readOnlyCluster.uuid == null) {
      String errMsg = "UUID of read-only cluster should be non-null.";
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    if (readOnlyCluster.clusterType != UniverseDefinitionTaskParams.ClusterType.ASYNC) {
      String errMsg =
          "Read-only cluster type should be "
              + UniverseDefinitionTaskParams.ClusterType.ASYNC
              + " but is "
              + readOnlyCluster.clusterType;
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    taskParams.clusters.add(primaryCluster);
    validateConsistency(primaryCluster, readOnlyCluster);

    // Set the provider code.
    Provider provider =
        Provider.getOrBadRequest(UUID.fromString(readOnlyCluster.userIntent.provider));
    readOnlyCluster.userIntent.providerType = Common.CloudType.valueOf(provider.code);
    readOnlyCluster.validate(
        !runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled"));

    TaskType taskType = TaskType.ReadOnlyClusterCreate;
    if (readOnlyCluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      try {
        checkK8sProviderAvailability(provider, customer);
        taskType = TaskType.ReadOnlyKubernetesClusterCreate;
      } catch (IllegalArgumentException e) {
        throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
      }
    }

    PlacementInfoUtil.updatePlacementInfo(
        taskParams.getNodesInCluster(readOnlyCluster.uuid), readOnlyCluster.placementInfo);

    // Submit the task to create the cluster.
    UUID taskUUID = commissioner.submit(taskType, taskParams);
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
    List<Cluster> existingNonPrimaryClusters =
        universe.getUniverseDetails().getNonPrimaryClusters();

    Cluster cluster =
        existingNonPrimaryClusters
            .stream()
            .filter(c -> c.uuid.equals(clusterUUID))
            .findFirst()
            .orElse(null);
    if (cluster == null) {
      String errMsg = "Uuid " + clusterUUID + " to delete cluster not found.";
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Create the Commissioner task to destroy the universe.
    UUID taskUUID;
    if (cluster.userIntent.providerType.equals(Common.CloudType.kubernetes)) {
      ReadOnlyKubernetesClusterDelete.Params taskParams =
          new ReadOnlyKubernetesClusterDelete.Params();
      taskParams.universeUUID = universe.universeUUID;
      taskParams.clusterUUID = clusterUUID;
      taskParams.isForceDelete = isForceDelete;
      taskParams.expectedUniverseVersion = universe.version;
      taskUUID = commissioner.submit(TaskType.ReadOnlyKubernetesClusterDelete, taskParams);
    } else {
      switch (cluster.clusterType) {
        case ASYNC:
          ReadOnlyClusterDelete.Params taskParams = new ReadOnlyClusterDelete.Params();
          taskParams.universeUUID = universe.universeUUID;
          taskParams.clusterUUID = clusterUUID;
          taskParams.isForceDelete = isForceDelete;
          taskParams.expectedUniverseVersion = universe.version;
          // Submit the task to delete the cluster.
          taskUUID = commissioner.submit(TaskType.ReadOnlyClusterDelete, taskParams);
          break;
        case ADDON:
          AddOnClusterDelete.Params addonParams = new AddOnClusterDelete.Params();
          addonParams.universeUUID = universe.universeUUID;
          addonParams.clusterUUID = clusterUUID;
          addonParams.isForceDelete = isForceDelete;
          addonParams.expectedUniverseVersion = universe.version;
          // Submit the task to delete the cluster.
          taskUUID = commissioner.submit(TaskType.AddOnClusterDelete, addonParams);
          break;
        default:
          throw new PlatformServiceException(BAD_REQUEST, "Invalid cluster type");
      }
    }

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
        List<Cluster> clusters = u.getUniverseDetails().getNonPrimaryClusters();
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

  void markAllUniverseTasksAsCompleted(UUID universeUUID) {
    for (CustomerTask task : CustomerTask.findIncompleteByTargetUUID(universeUUID)) {
      task.markAsCompleted();
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      if (taskInfo != null) {
        taskInfo.setTaskState(TaskInfo.State.Failure);
        taskInfo.save();
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
      taskType = TaskType.UpdateKubernetesDiskSize;
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

    boolean nodeToNodeChange =
        taskParams.enableNodeToNodeEncrypt != null
            && taskParams.enableNodeToNodeEncrypt != userIntent.enableNodeToNodeEncrypt;
    boolean clientToNodeChange =
        taskParams.enableClientToNodeEncrypt != null
            && taskParams.enableClientToNodeEncrypt != userIntent.enableClientToNodeEncrypt;
    boolean tlsToggle = (nodeToNodeChange || clientToNodeChange);

    boolean rootCaChange =
        taskParams.rootCA != null && !taskParams.rootCA.equals(universeDetails.rootCA);
    boolean rootAndClientRootCASameToggled =
        (taskParams.rootAndClientRootCASame != universeDetails.rootAndClientRootCASame);
    boolean clientRootCaChange =
        !taskParams.rootAndClientRootCASame
            && taskParams.getClientRootCA() != null
            && !taskParams.getClientRootCA().equals(universeDetails.getClientRootCA());
    boolean certsRotate =
        rootCaChange
            || clientRootCaChange
            || rootAndClientRootCASameToggled
            || taskParams.createNewRootCA
            || taskParams.createNewClientRootCA
            || taskParams.selfSignedServerCertRotate
            || taskParams.selfSignedClientCertRotate;

    if (tlsToggle && certsRotate) {
      if (((rootCaChange || taskParams.createNewRootCA) && universeDetails.rootCA != null)
          || ((clientRootCaChange || taskParams.createNewClientRootCA)
              && universeDetails.getClientRootCA() != null)) {
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

      // taskParams has the same subset of overridable fields as TlsToggleParams.
      // taskParams is already merged with universe details.
      TlsToggleParams tlsToggleParams;
      try {
        tlsToggleParams =
            Json.mapper().treeToValue(Json.mapper().valueToTree(taskParams), TlsToggleParams.class);
      } catch (JsonProcessingException e) {
        throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
      }

      tlsToggleParams.allowInsecure =
          !(taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt);
      tlsToggleParams.rootCA =
          isRootCA ? (!taskParams.createNewRootCA ? taskParams.rootCA : null) : null;
      tlsToggleParams.setClientRootCA(
          isClientRootCA
              ? (!taskParams.createNewClientRootCA ? taskParams.getClientRootCA() : null)
              : null);
      return upgradeUniverseHandler.toggleTls(tlsToggleParams, customer, universe);
    }

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
              runtimeConfigFactory.staticApplicationConf(),
              universeDetails.nodePrefix,
              customer.uuid);
    }

    if (isClientRootCA && taskParams.createNewClientRootCA) {
      taskParams.setClientRootCA(
          CertificateHelper.createClientRootCA(
              runtimeConfigFactory.staticApplicationConf(),
              universeDetails.nodePrefix,
              customer.uuid));
    }
    // taskParams has the same subset of overridable fields as CertsRotateParams.
    // taskParams is already merged with universe details.
    CertsRotateParams certsRotateParams;
    try {
      certsRotateParams =
          Json.mapper().treeToValue(Json.mapper().valueToTree(taskParams), CertsRotateParams.class);
    } catch (JsonProcessingException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    LOG.info("CertsRotateParams : {}", Json.toJson(CommonUtils.maskObject(certsRotateParams)));
    return upgradeUniverseHandler.rotateCerts(certsRotateParams, customer, universe);
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
    if (existingNode.cloudInfo != null
        && !Objects.equals(inputNode.cloudInfo.private_ip, existingNode.cloudInfo.private_ip)) {
      String errMsg =
          String.format(
              "Illegal attempt to change private ip to %s for node %s",
              inputNode.cloudInfo.private_ip, inputNode.nodeName);
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
    Set<String> forbiddenIps =
        Arrays.stream(appConfig.getString("yb.security.forbidden_ips", "").split("[, ]"))
            .filter(StringUtils::isNotBlank)
            .collect(Collectors.toSet());

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
                  if (node.cloudInfo != null
                      && node.cloudInfo.private_ip != null
                      && forbiddenIps.contains(node.cloudInfo.private_ip)) {
                    String errMsg =
                        String.format("Forbidden ip %s for node", node.cloudInfo.private_ip);
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
