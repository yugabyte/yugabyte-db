// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.YBUniverseStatus;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YcqlPassword;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YsqlPassword;
import java.util.Base64;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class YBUniverseReconciler extends AbstractReconciler<YBUniverse> {

  private final OperatorWorkQueue workqueue;
  private final SharedIndexInformer<YBUniverse> ybUniverseInformer;
  private final String namespace;
  private final Lister<YBUniverse> ybUniverseLister;
  private final MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      ybUniverseClient;
  public static final String APP_LABEL = "app";
  private final UniverseCRUDHandler universeCRUDHandler;
  private final UpgradeUniverseHandler upgradeUniverseHandler;
  private final CloudProviderHandler cloudProviderHandler;
  private final TaskExecutor taskExecutor;

  private final Integer reconcileExceptionBackoffMS = 5000;

  KubernetesOperatorStatusUpdater kubernetesStatusUpdater;

  public YBUniverseReconciler(
      KubernetesClient client,
      YBInformerFactory informerFactory,
      String namespace,
      UniverseCRUDHandler universeCRUDHandler,
      UpgradeUniverseHandler upgradeUniverseHandler,
      CloudProviderHandler cloudProviderHandler,
      TaskExecutor taskExecutor,
      KubernetesOperatorStatusUpdater kubernetesStatusUpdater) {
    super(client, informerFactory);
    this.ybUniverseClient = client.resources(YBUniverse.class);
    this.ybUniverseInformer = informerFactory.getSharedIndexInformer(YBUniverse.class, client);
    this.ybUniverseLister = new Lister<>(ybUniverseInformer.getIndexer());
    this.namespace = namespace;
    this.workqueue = new OperatorWorkQueue();
    this.universeCRUDHandler = universeCRUDHandler;
    this.upgradeUniverseHandler = upgradeUniverseHandler;
    this.cloudProviderHandler = cloudProviderHandler;
    this.kubernetesStatusUpdater = kubernetesStatusUpdater;
    this.taskExecutor = taskExecutor;
    this.ybUniverseInformer.addEventHandler(this);
  }

  @Override
  public void onAdd(YBUniverse universe) {
    enqueueYBUniverse(universe, OperatorWorkQueue.ResourceAction.CREATE);
  }

  @Override
  public void onUpdate(YBUniverse oldUniverse, YBUniverse newUniverse) {
    // Handle the delete workflow first, as we get a this call before onDelete is called
    if (!newUniverse
        .getMetadata()
        .getDeletionTimestamp()
        .equals(oldUniverse.getMetadata().getDeletionTimestamp())) {
      enqueueYBUniverse(newUniverse, OperatorWorkQueue.ResourceAction.DELETE);
      return;
    }
    ObjectMapper mapper = new ObjectMapper();
    try {
      String oldSpecJson = mapper.writeValueAsString(oldUniverse.getSpec());
      String newSpecJson = mapper.writeValueAsString(newUniverse.getSpec());
      if (oldSpecJson.equals(newSpecJson)) {
        log.trace("non-spec update detected, skipping update");
        return;
      }
    } catch (JsonProcessingException e) {
      log.warn("Failed to compare 2 universe specs", e);
    }
    enqueueYBUniverse(newUniverse, OperatorWorkQueue.ResourceAction.UPDATE);
  }

  @Override
  public void onDelete(YBUniverse universe, boolean b) {
    enqueueYBUniverse(universe, OperatorWorkQueue.ResourceAction.DELETE);
  }

  public void run() {
    log.info("Starting YBUniverse controller thread");
    // Wait for the informer to "sync" for the first time. Syncing here roughly means the informer
    // has gotten data from the k8s api.
    while (!Thread.currentThread().isInterrupted()) {
      if (ybUniverseInformer.hasSynced()) {
        break;
      }
    }

    while (true) {
      try {
        log.info("trying to fetch universe actions from workqueue...");
        if (workqueue.isEmpty()) {
          log.debug("Work Queue is empty");
        }
        Pair<String, OperatorWorkQueue.ResourceAction> pair = workqueue.pop();
        String key = pair.getFirst();
        OperatorWorkQueue.ResourceAction action = pair.getSecond();
        Objects.requireNonNull(key, "The workqueue item key can't be null.");
        log.debug("Found work item: {} {}", key, action);
        if ((!key.contains("/"))) {
          log.warn("invalid resource key: {}", key);
          continue;
        }

        // Get the YBUniverse resource's name
        // from key which is in format namespace/name.
        String name = getUniverseName(key);
        YBUniverse ybUniverse = ybUniverseLister.get(key);
        log.info("Processing YbUniverse Object: {} {}", name, ybUniverse);

        // Handle new calls that can happen from removing the finalizer or any other minor status
        // updates.
        if (ybUniverse == null && action == OperatorWorkQueue.ResourceAction.DELETE) {
          log.info("Tried to delete ybUniverse but it's no longer in Lister");
          continue;
        }
        reconcile(ybUniverse, action);

      } catch (Exception e) {
        log.error("Got Exception {}", e);
        try {
          Thread.sleep(reconcileExceptionBackoffMS);
          log.info("continue ybuniverse reconcile loop after failure backoff");
        } catch (InterruptedException e1) {
          log.info("caught interrupt, stopping reconciler", e);
          break;
        }
      }
    }
  }

  /**
   * Tries to achieve the desired state for ybUniverse.
   *
   * @param ybUniverse specified ybUniverse
   */
  protected void reconcile(YBUniverse ybUniverse, OperatorWorkQueue.ResourceAction action) {
    String universeName = ybUniverse.getMetadata().getName();
    String namespace = ybUniverse.getMetadata().getNamespace();
    log.info(
        "Reconcile for YbUniverse metadata: Name = {}, Namespace = {}", universeName, namespace);

    try {
      List<Customer> custList = Customer.getAll();
      if (custList.size() != 1) {
        throw new Exception("Customer list does not have exactly one customer.");
      }
      Customer cust = custList.get(0);
      // checking to see if the universe was deleted.
      if (action == OperatorWorkQueue.ResourceAction.DELETE) {
        log.info("deleting universe {}", universeName);
        UniverseResp universeResp =
            universeCRUDHandler.findByName(cust, universeName).stream().findFirst().orElse(null);

        if (universeResp == null) {
          log.debug("universe {} already deleted in YBA, cleaning up", universeName);
          YBUniverseStatus ybUniStatus = ybUniverse.getStatus();
          // at this point the universe should be deleted, lets just return.
          if (ybUniStatus == null) {
            return;
          }

          // Add thread to delete provider and remove finalizer
          ObjectMeta objectMeta = ybUniverse.getMetadata();
          if (CollectionUtils.isNotEmpty(objectMeta.getFinalizers())) {
            UUID customerUUID = cust.getUuid();
            Thread universeDeletionFinalizeThread =
                new Thread(
                    () -> {
                      try {
                        if (canDeleteProvider(cust, universeName)) {
                          try {
                            UUID deleteProviderTaskUUID =
                                deleteProvider(customerUUID, universeName);
                            taskExecutor.waitForTask(deleteProviderTaskUUID);
                          } catch (Exception e) {
                            log.error("Got error in deleting provider", e);
                          }
                        }
                        log.info("Removing finalizers...");
                        if (ybUniverse.getStatus() != null) {
                          objectMeta.setFinalizers(Collections.emptyList());
                          ybUniverseClient
                              .inNamespace(namespace)
                              .withName(universeName)
                              .patch(ybUniverse);
                        }
                      } catch (Exception e) {
                        log.info("Got error in finalizing Universe {} delete", universeName);
                      }
                    });
            universeDeletionFinalizeThread.start();
          }
        } else {
          log.debug("deleting universe {} in yba", universeName);
          Universe universe = Universe.getOrBadRequest(universeResp.universeUUID);
          UUID universeUUID = universe.getUniverseUUID();
          Result task = deleteUniverse(cust.getUuid(), universeUUID, ybUniverse);
          if (task != null) {
            log.info("Deleted Universe using KubernetesOperator");
            log.info(task.toString());
          }
        }
      } else if (action == OperatorWorkQueue.ResourceAction.CREATE) {
        log.info("Creating new universe {}", universeName);

        // Universe does not exist.
        // We can get multiple create calls - though typically not at the same time. When the
        // operator starts, all existing custom resources will have a new create call sent by the
        // informer. We handle this by calling into 'edit' if the universe already exists in YBA.
        // TODO: We may need to retry the create call, for cases where YBA restarted before the
        // universe was fully deployed.
        if (universeCRUDHandler.findByName(cust, ybUniverse.getMetadata().getName()).isEmpty()) {
          // Allowing us to update the status of the ybUniverse
          // Setting finalizer to prevent out-of-operator deletes of custom resources
          ObjectMeta objectMeta = ybUniverse.getMetadata();
          objectMeta.setFinalizers(Collections.singletonList("finalizer.k8soperator.yugabyte.com"));
          ybUniverseClient
              .inNamespace(namespace)
              .withName(ybUniverse.getMetadata().getName())
              .patch(ybUniverse);
          UniverseConfigureTaskParams taskParams = createTaskParams(ybUniverse, cust.getUuid());
          Result task = createUniverse(cust.getUuid(), taskParams, ybUniverse);
          log.info("Created Universe KubernetesOperator " + task.toString());
        } else {
          log.debug("universe {} already exists, treating as update", universeName);
          Optional<Universe> u =
              Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName());
          u.ifPresent(
              universe -> {
                editUniverse(cust, universe, ybUniverse); /* gives null Result if not a real edit */
              });
        }
      } else if (action == OperatorWorkQueue.ResourceAction.UPDATE) {
        log.info("Updating universe {}", universeName);
        Optional<Universe> u = Universe.maybeGetUniverseByName(cust.getId(), universeName);
        if (!u.isPresent()) {
          log.error("universe {} does not exist, cannot update", universeName);
          throw new Exception("cannot update universe which does not exist");
        }
        Universe universe = u.get();
        editUniverse(cust, universe, ybUniverse); /* gives null Result if not a real edit */
      }
    } catch (Exception e) {
      log.error("Got Exception in Operator Action", e);
    }
  }

  private Result deleteUniverse(UUID customerUUID, UUID universeUUID, YBUniverse ybUniverse) {
    log.info("Deleting universe using operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    /* customer, universe, isForceDelete, isDeleteBackups, isDeleteAssociatedCerts */
    if (!universe.getUniverseDetails().updateInProgress) {
      KubernetesResourceDetails resourceDetails =
          KubernetesResourceDetails.fromResource(ybUniverse);
      UUID taskUUID =
          universeCRUDHandler.destroy(customer, universe, false, false, false, resourceDetails);
      return new YBPTask(taskUUID, universeUUID).asResult();
    } else {
      log.info("Delete in progress, not deleting universe");
      return null;
    }
  }

  private UUID deleteProvider(UUID customerUUID, String universeName) {
    log.info("Deleting provider using operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    String providerName = getProviderName(universeName);
    Provider provider = Provider.get(customer.getUuid(), providerName, CloudType.kubernetes);
    return cloudProviderHandler.delete(customer, provider.getUuid());
  }

  private boolean canDeleteProvider(Customer customer, String universeName) {
    log.info("Checking if provider can be deleted");
    String providerName = getProviderName(universeName);
    Provider provider = Provider.get(customer.getUuid(), providerName, CloudType.kubernetes);
    return (provider != null) && (customer.getUniversesForProvider(provider.getUuid()).size() == 0);
  }

  private Result createUniverse(
      UUID customerUUID, UniverseConfigureTaskParams taskParams, YBUniverse ybUniverse) {
    log.info("creating universe via k8s operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    taskParams.isKubernetesOperatorControlled = true;
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    taskParams.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, taskParams);

    log.info("Done configuring CRUDHandler");

    if (taskParams.clusters.stream()
        .anyMatch(cluster -> cluster.clusterType == ClusterType.ASYNC)) {
      taskParams.currentClusterType = ClusterType.ASYNC;
      universeCRUDHandler.configure(customer, taskParams);
    }

    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    log.info("Done creating universe through CRUD Handler");
    return new YBPTask(universeResp.taskUUID, universeResp.universeUUID).asResult();
  }

  private void editUniverse(Customer cust, Universe universe, YBUniverse ybUniverse) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (universeDetails == null || universeDetails.getPrimaryCluster() == null) {
      throw new RuntimeException(
          String.format("invalid universe details found for {}", universe.getName()));
    }

    UserIntent currentUserIntent = universeDetails.getPrimaryCluster().userIntent;
    UserIntent incomingIntent = createUserIntent(ybUniverse, cust.getUuid());

    // Fix non-changeable values to current.
    incomingIntent.accessKeyCode = currentUserIntent.accessKeyCode;
    incomingIntent.enableExposingService = currentUserIntent.enableExposingService;

    // Updating cluster with new userIntent info
    Cluster primaryCluster = universeDetails.getPrimaryCluster();
    primaryCluster.userIntent = incomingIntent;
    universeDetails.clusters =
        universeDetails.clusters.stream()
            .filter(c -> !c.clusterType.equals(ClusterType.PRIMARY))
            .collect(Collectors.toList());
    universeDetails.clusters.add(primaryCluster);

    // Add kubernetes event.
    String startingTask =
        String.format("Starting task on universe {}", currentUserIntent.universeName);
    kubernetesStatusUpdater.doKubernetesEventUpdate(
        KubernetesResourceDetails.fromResource(ybUniverse), startingTask);

    if (!incomingIntent.universeOverrides.equals(currentUserIntent.universeOverrides)) {
      if (checkAndHandleUniverseLock(universe, OperatorWorkQueue.ResourceAction.UPDATE)) {
        return;
      }
      log.info("Updating Kubernetes Overrides");
      updateOverridesYbUniverse(
          universeDetails, cust, ybUniverse, incomingIntent.universeOverrides);
    } else if (!(currentUserIntent.masterGFlags.equals(incomingIntent.masterGFlags))
        || !(currentUserIntent.tserverGFlags.equals(incomingIntent.tserverGFlags))) {
      if (checkAndHandleUniverseLock(universe, OperatorWorkQueue.ResourceAction.UPDATE)) {
        return;
      }
      log.info("Updating Gflags");
      updateGflagsYbUniverse(
          universeDetails,
          cust,
          ybUniverse,
          incomingIntent.masterGFlags,
          incomingIntent.tserverGFlags);
    } else if (currentUserIntent.numNodes != incomingIntent.numNodes) {
      if (checkAndHandleUniverseLock(universe, OperatorWorkQueue.ResourceAction.UPDATE)) {
        return;
      }
      log.info("Updating nodes");
      updateYBUniverse(universeDetails, cust, ybUniverse);
    } else if (!currentUserIntent.ybSoftwareVersion.equals(incomingIntent.ybSoftwareVersion)) {
      if (checkAndHandleUniverseLock(universe, OperatorWorkQueue.ResourceAction.UPDATE)) {
        return;
      }
      log.info("Upgrading software");
      upgradeYBUniverse(universeDetails, cust, ybUniverse);
    } else {
      if (checkAndHandleUniverseLock(universe, OperatorWorkQueue.ResourceAction.UPDATE)) {
        return;
      }
      log.info("No update made");
    }
  }

  private UUID updateOverridesYbUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      String universeOverrides) {
    KubernetesOverridesUpgradeParams requestParams = new KubernetesOverridesUpgradeParams();

    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    try {
      requestParams =
          mapper.readValue(
              mapper.writeValueAsString(taskParams), KubernetesOverridesUpgradeParams.class);
    } catch (Exception e) {
      log.error("Failed at creating upgrade software params", e);
    }
    requestParams.universeOverrides = universeOverrides;

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);

    UUID taskUUID =
        upgradeUniverseHandler.upgradeKubernetesOverrides(requestParams, cust, oldUniverse);
    log.info("Submitted task to upgrade universe overrides with new overrides");
    return taskUUID;
  }

  private UUID updateGflagsYbUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      Map<String, String> masterGflags,
      Map<String, String> tserverGflags) {
    KubernetesGFlagsUpgradeParams requestParams = new KubernetesGFlagsUpgradeParams();

    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    try {
      requestParams =
          mapper.readValue(
              mapper.writeValueAsString(taskParams), KubernetesGFlagsUpgradeParams.class);
    } catch (Exception e) {
      log.error("Failed at creating upgrade software params", e);
    }
    requestParams.masterGFlags = masterGflags;
    requestParams.tserverGFlags = tserverGflags;

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);

    UUID taskUUID = upgradeUniverseHandler.upgradeGFlags(requestParams, cust, oldUniverse);
    log.info("Submitted task to upgrade universe with new GFlags");
    return taskUUID;
  }

  private UUID upgradeYBUniverse(
      UniverseDefinitionTaskParams taskParams, Customer cust, YBUniverse ybUniverse) {
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    SoftwareUpgradeParams requestParams = new SoftwareUpgradeParams();
    requestParams.ybSoftwareVersion = taskParams.getPrimaryCluster().userIntent.ybSoftwareVersion;
    try {
      requestParams =
          mapper.readValue(mapper.writeValueAsString(taskParams), SoftwareUpgradeParams.class);
    } catch (Exception e) {
      log.error("Failed at creating upgrade software params", e);
    }

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);

    // requestParams.taskType = UpgradeTaskParams.UpgradeTaskType.Software;
    // requestParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    requestParams.ybSoftwareVersion = taskParams.getPrimaryCluster().userIntent.ybSoftwareVersion;
    requestParams.setUniverseUUID(oldUniverse.getUniverseUUID());
    log.info("Upgrading universe with new info now");
    return upgradeUniverseHandler.upgradeSoftware(requestParams, cust, oldUniverse);
  }

  private UUID updateYBUniverse(
      UniverseDefinitionTaskParams taskParams, Customer cust, YBUniverse ybUniverse) {
    // Converting details to configure task params using JSON
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    UniverseConfigureTaskParams taskConfigParams = null;
    try {
      taskConfigParams =
          mapper.readValue(
              mapper.writeValueAsString(taskParams), UniverseConfigureTaskParams.class);
    } catch (Exception e) {
      log.error("Failed at creating configure task params for edit", e);
    }

    taskConfigParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.EDIT;
    taskConfigParams.currentClusterType = ClusterType.PRIMARY;
    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);
    log.info("Updating universe with new info now");
    universeCRUDHandler.configure(cust, taskConfigParams);
    return universeCRUDHandler.update(cust, oldUniverse, taskConfigParams);
  }

  private UniverseConfigureTaskParams createTaskParams(YBUniverse ybUniverse, UUID customerUUID)
      throws Exception {
    log.info("Creating task params");
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    Cluster cluster = new Cluster(ClusterType.PRIMARY, createUserIntent(ybUniverse, customerUUID));
    taskParams.clusters.add(cluster);
    List<Users> users = Users.getAll(customerUUID);
    if (users.isEmpty()) {
      log.error("Users list is of size 0!");
      throw new Exception("Need at least one user");
    } else {
      log.info("Taking first user for customer");
    }
    taskParams.creatingUser = users.get(0);
    // CommonUtils.getUserFromContext(ctx);
    taskParams.expectedUniverseVersion = -1; // -1 skips the version check
    taskParams.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(ybUniverse));
    return taskParams;
  }

  private UserIntent createUserIntent(YBUniverse ybUniverse, UUID customerUUID) {
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = ybUniverse.getMetadata().getName();
    ObjectMapper mapper = new ObjectMapper(new YAMLFactory());
    mapper.setSerializationInclusion(Include.NON_NULL);
    mapper.setSerializationInclusion(Include.NON_EMPTY);
    try {
      userIntent.universeOverrides =
          mapper.writeValueAsString(ybUniverse.getSpec().getKubernetesOverrides());
    } catch (Exception e) {
      log.error("Unable to parse universe overrides", e);
    }
    Provider provider = getProvider(customerUUID, ybUniverse);
    userIntent.provider = provider.getUuid().toString();
    userIntent.providerType = CloudType.kubernetes;
    userIntent.replicationFactor =
        ybUniverse.getSpec().getReplicationFactor() != null
            ? ((int) ybUniverse.getSpec().getReplicationFactor().longValue())
            : 0;
    userIntent.regionList =
        provider.getRegions().stream().map(r -> r.getUuid()).collect(Collectors.toList());
    ;
    // userIntent.preferredRegion = preferredRegion;
    userIntent.instanceType = ybUniverse.getSpec().getInstanceType();
    userIntent.numNodes =
        ybUniverse.getSpec().getNumNodes() != null
            ? ((int) ybUniverse.getSpec().getNumNodes().longValue())
            : 0;
    userIntent.ybSoftwareVersion = ybUniverse.getSpec().getYbSoftwareVersion();
    userIntent.accessKeyCode = "";
    userIntent.assignPublicIP = ybUniverse.getSpec().getAssignPublicIP();

    userIntent.deviceInfo = mapDeviceInfo(ybUniverse.getSpec().getDeviceInfo());
    log.debug("ui.deviceInfo : {}", userIntent.deviceInfo);
    log.debug("given deviceInfo: {} ", ybUniverse.getSpec().getDeviceInfo());

    userIntent.useTimeSync = ybUniverse.getSpec().getUseTimeSync();
    userIntent.enableYSQL = ybUniverse.getSpec().getEnableYSQL();
    userIntent.enableYEDIS = ybUniverse.getSpec().getEnableYEDIS();
    userIntent.enableNodeToNodeEncrypt = ybUniverse.getSpec().getEnableNodeToNodeEncrypt();
    userIntent.enableClientToNodeEncrypt = ybUniverse.getSpec().getEnableClientToNodeEncrypt();
    userIntent.kubernetesOperatorVersion = ybUniverse.getMetadata().getGeneration();

    // Handle Passwords
    YsqlPassword ysqlPassword = ybUniverse.getSpec().getYsqlPassword();
    if (ysqlPassword != null) {
      Secret ysqlSecret = getSecret(ysqlPassword.getSecretName());
      String password = parseSecretForKey(ysqlSecret, "ysqlPassword");
      if (password == null) {
        log.error("could not find ysqlPassword in secret {}", ysqlPassword.getSecretName());
        throw new RuntimeException(
            "could not find ysqlPassword in secret " + ysqlPassword.getSecretName());
      }
      userIntent.enableYSQLAuth = true;
      userIntent.ysqlPassword = password;
    }
    YcqlPassword ycqlPassword = ybUniverse.getSpec().getYcqlPassword();
    if (ycqlPassword != null) {
      Secret ycqlSecret = getSecret(ycqlPassword.getSecretName());
      String password = parseSecretForKey(ycqlSecret, "ycqlPassword");
      if (password == null) {
        log.error("could not find ycqlPassword in secret {}", ycqlPassword.getSecretName());
        throw new RuntimeException(
            "could not find ycqlPassword in secret " + ycqlPassword.getSecretName());
      }
      userIntent.enableYCQLAuth = true;
      userIntent.ycqlPassword = password;
    }
    if (ybUniverse.getSpec().getMasterGFlags() != null) {
      userIntent.masterGFlags = new HashMap<>(ybUniverse.getSpec().getMasterGFlags());
    }
    if (ybUniverse.getSpec().getTserverGFlags() != null) {
      userIntent.tserverGFlags = new HashMap<>(ybUniverse.getSpec().getTserverGFlags());
    }
    return userIntent;
  }

  // getSecret find a secret in the namespace an operator is listening on.
  private Secret getSecret(String name) {
    if (!namespace.trim().isEmpty()) {
      return client.secrets().inNamespace(namespace.trim()).withName(name).get();
    }
    return client.secrets().inNamespace("default").withName(name).get();
  }

  // parseSecretForKey checks secret data for the key. If not found, it will then check stringData.
  // Returns null if the key is not found at all.
  // Also handles null secret.
  private String parseSecretForKey(Secret secret, String key) {
    if (secret == null) {
      return null;
    }
    if (secret.getData().get(key) != null) {
      return new String(Base64.getDecoder().decode(secret.getData().get(key)));
    }
    return secret.getStringData().get(key);
  }

  private Provider createAutoProvider(
      YBUniverse ybUniverse, String providerName, UUID customerUUID) {
    List<String> zonesFilter = ybUniverse.getSpec().getZoneFilter();
    KubernetesProviderFormData providerData = cloudProviderHandler.suggestedKubernetesConfigs();
    providerData.regionList =
        providerData.regionList.stream()
            .map(
                r -> {
                  if (zonesFilter != null) {
                    List<KubernetesProviderFormData.RegionData.ZoneData> filteredZones =
                        r.zoneList.stream()
                            .filter(z -> zonesFilter.contains(z.name))
                            .collect(Collectors.toList());

                    r.zoneList = filteredZones;
                  }
                  r.zoneList =
                      r.zoneList.stream()
                          .map(
                              z -> {
                                HashMap<String, String> tempMap = new HashMap<>(z.config);
                                tempMap.put("STORAGE_CLASS", "yb-standard");
                                z.config = tempMap;
                                return z;
                              })
                          .collect(Collectors.toList());
                  return r;
                })
            .collect(Collectors.toList());
    providerData.name = providerName;
    Provider autoProvider =
        cloudProviderHandler.createKubernetes(Customer.getOrBadRequest(customerUUID), providerData);
    // Fetch created provider from DB.
    return Provider.get(customerUUID, providerName, CloudType.kubernetes);
  }

  private Provider filterProviders(YBUniverse ybUniverse, UUID customerUUID) {
    Provider provider = null;
    List<Provider> providers =
        Provider.getAll(customerUUID).stream()
            .filter(p -> p.getCloudCode() == CloudType.kubernetes)
            .collect(Collectors.toList());
    if (!providers.isEmpty()) {
      if (!CollectionUtils.isNotEmpty(ybUniverse.getSpec().getZoneFilter())) {
        log.error("Zone filter is not supported with pre-existing providers, ignoring zone filter");
      }
      provider = providers.get(0);
    }
    return provider;
  }

  private Provider getProvider(UUID customerUUID, YBUniverse ybUniverse) {
    Provider provider = null;
    // Check if provider name available in Cr
    String providerName = ybUniverse.getSpec().getProviderName();
    if (StringUtils.isNotBlank(providerName)) {
      // Case when provider name is available: Use that, or fail.
      provider = Provider.get(customerUUID, providerName, CloudType.kubernetes);
      if (provider != null) {
        log.info("Using provider from custom resource spec.");
        return provider;
      } else {
        throw new RuntimeException(
            "Could not find provider " + providerName + " in the list of providers.");
      }
    } else {
      // Case when provider name is not available in spec
      providerName = getProviderName(ybUniverse.getMetadata().getName());
      provider = Provider.get(customerUUID, providerName, CloudType.kubernetes);
      if (provider != null) {
        // If auto-provider with the same name found return it.
        log.info("Found auto-provider existing with same name {}", providerName);
        return provider;
      } else {
        // If not found:
        if (isRunningInKubernetes()) {
          // Create auto-provider for Kubernetes based installation
          log.info("Creating auto-provider with name {}", providerName);
          try {
            return createAutoProvider(ybUniverse, providerName, customerUUID);
          } catch (Exception e) {
            throw new RuntimeException("Unable to create auto-provider", e);
          }
        } else {
          throw new RuntimeException("No usable providers found!");
        }
      }
    }
  }

  private void enqueueYBUniverse(YBUniverse ybUniverse, OperatorWorkQueue.ResourceAction action) {
    String uniName =
        String.format(
            "%s/%s", ybUniverse.getMetadata().getNamespace(), ybUniverse.getMetadata().getName());
    log.debug("enqueue universe {} with action {}", uniName, action.toString());
    workqueue.add(new Pair<String, OperatorWorkQueue.ResourceAction>(uniName, action));
  }

  private String getUniverseName(String key) throws Exception {
    if (key == null) {
      throw new Exception("Kubernetes Operator Universe key was null");
    }
    String[] keyList = key.split("/");
    if (keyList.length < 2) {
      throw new Exception("Kubernetes Operator Universe key did not have / to split on");
    }
    return keyList[1];
  }

  private boolean isRunningInKubernetes() {
    String kubernetesServiceHost = KubernetesEnvironmentVariables.getServiceHost();
    String kubernetesServicePort = KubernetesEnvironmentVariables.getServicePort();

    return (kubernetesServiceHost != null && kubernetesServicePort != null);
  }

  private String getProviderName(String universeName) {
    return ("prov-" + universeName);
  }

  private DeviceInfo mapDeviceInfo(io.yugabyte.operator.v1alpha1.ybuniversespec.DeviceInfo spec) {
    DeviceInfo di = new DeviceInfo();

    Long numVols = spec.getNumVolumes();
    if (numVols != null) {
      di.numVolumes = numVols.intValue();
    }

    Long diskIops = spec.getDiskIops();
    if (diskIops != null) {
      di.diskIops = diskIops.intValue();
    }

    Long throughput = spec.getThroughput();
    if (throughput != null) {
      di.throughput = throughput.intValue();
    }

    Long volSize = spec.getVolumeSize();
    if (volSize != null) {
      di.volumeSize = volSize.intValue();
    }

    io.yugabyte.operator.v1alpha1.ybuniversespec.DeviceInfo.StorageType st = spec.getStorageType();
    if (st != null) {
      di.storageType = StorageType.fromString(st.getValue());
    }

    di.mountPoints = spec.getMountPoints();
    di.storageClass = spec.getStorageClass();

    return di;
  }

  private boolean checkAndHandleUniverseLock(
      Universe universe, OperatorWorkQueue.ResourceAction action) {
    log.trace("checking if universe has active tasks");
    // TODO: Is `universeIsLocked()` enough to check here?
    if (universe.universeIsLocked()) {
      log.warn("universe {} is locked, requeue update and try again later", universe.getName());
      workqueue.requeue(String.format("%s/%s", namespace, universe.getName()), action);
      log.debug("scheduled universe update for requeue");
      return true;
    }
    return false;
  }
}
