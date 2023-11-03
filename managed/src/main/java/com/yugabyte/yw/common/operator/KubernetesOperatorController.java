// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
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
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Cache;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YcqlPassword;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YsqlPassword;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.Pair;
import play.libs.Json;
import play.mvc.Result;

public class KubernetesOperatorController {

  private static final long MAX_OPERATOR_STARTUP_TIME = 10; // seconds
  public static final int WORKQUEUE_CAPACITY = 1024;
  // String is key of form namespace/name.
  // OperatorAction is CREATE, EDIT, or DELETE corresponding to CR action.
  private final BlockingQueue<Pair<String, OperatorAction>> workqueue;
  private final SharedIndexInformer<YBUniverse> ybUniverseInformer;
  private final String namespace;
  private final Lister<YBUniverse> ybUniverseLister;
  private final MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      ybUniverseClient;
  public static final String APP_LABEL = "app";
  private final KubernetesClient kubernetesClient;
  private final UniverseCRUDHandler universeCRUDHandler;
  private final UpgradeUniverseHandler upgradeUniverseHandler;
  private final CloudProviderHandler cloudProviderHandler;
  private Map<String, Deque<Pair<Field, UserIntent>>> pendingTasks = new HashMap<>();

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesOperatorController.class);

  OperatorStatusUpdater kubernetesStatusUpdater;

  public enum OperatorAction {
    CREATED,
    UPDATED,
    DELETED
  }

  public KubernetesOperatorController(
      KubernetesClient kubernetesClient,
      MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
          ybUniverseClient,
      SharedIndexInformer<YBUniverse> ybUniverseInformer,
      String namespace,
      UniverseCRUDHandler universeCRUDHandler,
      UpgradeUniverseHandler upgradeUniverseHandler,
      CloudProviderHandler cloudProviderHandler,
      OperatorStatusUpdaterFactory statusUpdaterFactory) {
    this.kubernetesClient = kubernetesClient;
    this.ybUniverseClient = ybUniverseClient;
    this.ybUniverseLister = new Lister<>(ybUniverseInformer.getIndexer());
    this.ybUniverseInformer = ybUniverseInformer;
    this.namespace = namespace;
    this.workqueue = new ArrayBlockingQueue<>(WORKQUEUE_CAPACITY);
    this.universeCRUDHandler = universeCRUDHandler;
    this.upgradeUniverseHandler = upgradeUniverseHandler;
    this.cloudProviderHandler = cloudProviderHandler;
    this.kubernetesStatusUpdater = statusUpdaterFactory.create();
    addEventHandlersToSharedIndexInformers();
  }

  public void run() {
    LOG.info("Starting YBUniverse controller");
    while (!Thread.currentThread().isInterrupted()) {
      if (ybUniverseInformer.hasSynced()) {
        break;
      }
    }

    while (true) {
      try {
        LOG.info("trying to fetch item from workqueue...");
        if (workqueue.isEmpty()) {
          LOG.info("Work Queue is empty");
        }
        Pair<String, OperatorAction> pair = workqueue.take();
        String key = pair.getFirst();
        OperatorAction action = pair.getSecond();
        Objects.requireNonNull(key, "The workqueue item key can't be null.");
        LOG.info("Got {} {}", key, action);
        if ((!key.contains("/"))) {
          LOG.warn("invalid resource key: {}", key);
        }

        // Get the YBUniverse resource's name
        // from key which is in format namespace/name.
        String name = getUniverseName(key);
        YBUniverse ybUniverse = ybUniverseLister.get(key);
        LOG.info("Processing YbUniverse Object: {} {}", name, ybUniverse);
        if (ybUniverse == null) {
          if (action == OperatorAction.DELETED) {
            LOG.info("Tried to delete ybUniverse but it's no longer in Lister");
            continue;
          }
        }
        String universeName = name;
        reconcile(ybUniverse, action);

      } catch (Exception e) {
        LOG.error("Got Exception {}", e);
        Thread.currentThread().interrupt();
      }
    }
  }

  /**
   * Tries to achieve the desired state for ybUniverse.
   *
   * @param ybUniverse specified ybUniverse
   */
  protected void reconcile(YBUniverse ybUniverse, OperatorAction action) {
    String universeName = ybUniverse.getMetadata().getName();
    String namespace = ybUniverse.getMetadata().getNamespace();
    LOG.info(
        "Reconcile for YbUniverse metadata: Name = {}, Namespace = {}", universeName, namespace);

    try {
      List<Customer> custList = Customer.getAll();
      if (custList.size() != 1) {
        throw new Exception("Customer list does not have exactly one customer.");
      }
      Customer cust = custList.get(0);
      // checking to see if the universe was deleted.
      if (ybUniverse.getMetadata().getDeletionTimestamp() != null) {

        LOG.info(universeName);
        UniverseResp universeResp =
            universeCRUDHandler.findByName(cust, universeName).stream().findFirst().orElse(null);

        if (universeResp == null) {
          var ybUniStatus = ybUniverse.getStatus();
          // at this point the universe should be deleted, lets just return.
          if (ybUniStatus == null) {
            return;
          }
          String status = ybUniStatus.getUniverseStatus();
          if (status.contains("DestroyKubernetesUniverse Success")
              && canDeleteProvider(cust, universeName)
              && isRunningInKubernetes()) {
            LOG.info("Status is: " + status);
            LOG.info("Deleting provider now");
            Result deleteProvider = deleteProvider(cust.getUuid(), universeName);
            // Removing finalizer so we can delete the custom resource
            // This only happens after we remove the corresponding provider
            ObjectMeta objectMeta = ybUniverse.getMetadata();
            objectMeta.setFinalizers(Collections.emptyList());
            ybUniverseClient
                .inNamespace(namespace)
                .withName(ybUniverse.getMetadata().getName())
                .patch(ybUniverse);
          }
        } else {
          Universe universe = Universe.getOrBadRequest(universeResp.universeUUID);
          UUID universeUUID = universe.getUniverseUUID();
          Result task = deleteUniverse(cust.getUuid(), universeUUID, ybUniverse);

          if (!isRunningInKubernetes()) {
            ObjectMeta objectMeta = ybUniverse.getMetadata();
            objectMeta.setFinalizers(Collections.emptyList());
            ybUniverseClient
                .inNamespace(namespace)
                .withName(ybUniverse.getMetadata().getName())
                .patch(ybUniverse);
          }
          if (task != null) {
            LOG.info("Deleted Universe using KubernetesOperator");
            LOG.info(task.toString());
          }
        }
      } else if (action == OperatorAction.CREATED) {
        // Allowing us to update the status of the ybUniverse
        // Setting finalizer to prevent out-of-operator deletes of custom resources
        ObjectMeta objectMeta = ybUniverse.getMetadata();
        objectMeta.setFinalizers(Collections.singletonList("finalizer.k8soperator.yugabyte.com"));
        ybUniverseClient
            .inNamespace(namespace)
            .withName(ybUniverse.getMetadata().getName())
            .patch(ybUniverse);

        if (universeCRUDHandler.findByName(cust, ybUniverse.getMetadata().getName()).isEmpty()) {
          UniverseConfigureTaskParams taskParams = createTaskParams(ybUniverse, cust.getUuid());
          Result task = createUniverse(cust.getUuid(), taskParams, ybUniverse);
          LOG.info("Created Universe KubernetesOperator");
          LOG.info(task.toString());
        } else {
          Optional<Universe> u =
              Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName());
          u.ifPresent(
              universe -> {
                editUniverse(cust, universe, ybUniverse); /* gives null Result if not a real edit */
              });
        }
      } else if (action == OperatorAction.UPDATED) {
        LOG.info("Update action - non-delete");
        Optional<Universe> u = Universe.maybeGetUniverseByName(cust.getId(), universeName);
        u.ifPresent(
            universe -> {
              editUniverse(cust, universe, ybUniverse); /* gives null Result if not a real edit */
            });
      }
    } catch (Exception e) {
      LOG.error("Got Exception in Operator Action", e);
    }
  }

  private Result deleteUniverse(UUID customerUUID, UUID universeUUID, YBUniverse ybUniverse) {
    LOG.info("Deleting universe using operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    // remove pending tasks from map as we are deleting the universe
    Optional.ofNullable(pendingTasks.get(universe.getName()))
        .ifPresent(v -> pendingTasks.remove(universe.getName()));

    /* customer, universe, isForceDelete, isDeleteBackups, isDeleteAssociatedCerts */
    if (!universe.getUniverseDetails().updateInProgress) {
      KubernetesResourceDetails resourceDetails =
          KubernetesResourceDetails.fromResource(ybUniverse);
      UUID taskUUID =
          universeCRUDHandler.destroy(customer, universe, false, false, false, resourceDetails);
      return new YBPTask(taskUUID, universeUUID).asResult();
    } else {
      LOG.info("Delete in progress, not deleting universe");
      return null;
    }
  }

  private Result deleteProvider(UUID customerUUID, String universeName) {
    LOG.info("Deleting provider using operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    String providerName = getProviderName(universeName);
    Provider provider = Provider.get(customer.getUuid(), providerName, CloudType.kubernetes);
    UUID taskUUID = cloudProviderHandler.delete(customer, provider.getUuid());
    return new YBPTask(taskUUID, provider.getUuid()).asResult();
  }

  private boolean canDeleteProvider(Customer customer, String universeName) {
    LOG.info("Checking if provider can be deleted");
    String providerName = getProviderName(universeName);
    Provider provider = Provider.get(customer.getUuid(), providerName, CloudType.kubernetes);
    return (customer.getUniversesForProvider(provider.getUuid()).size() == 0);
  }

  private Result createUniverse(
      UUID customerUUID, UniverseConfigureTaskParams taskParams, YBUniverse ybUniverse) {
    LOG.info("creating universe via k8s operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    taskParams.isKubernetesOperatorControlled = true;
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    taskParams.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, taskParams);

    LOG.info("Done configuring CRUDHandler");

    if (taskParams.clusters.stream()
        .anyMatch(cluster -> cluster.clusterType == ClusterType.ASYNC)) {
      taskParams.currentClusterType = ClusterType.ASYNC;
      universeCRUDHandler.configure(customer, taskParams);
    }

    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    LOG.info("Done creating universe through CRUD Handler");
    return new YBPTask(universeResp.taskUUID, universeResp.universeUUID).asResult();
  }

  private void editUniverse(Customer cust, Universe universe, YBUniverse ybUniverse) {
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    if (taskParams != null && taskParams.getPrimaryCluster() != null) {
      UserIntent currentUserIntent = taskParams.getPrimaryCluster().userIntent;
      UserIntent incomingIntent = createUserIntent(ybUniverse, cust.getUuid());
      incomingIntent.accessKeyCode = currentUserIntent.accessKeyCode;
      incomingIntent.enableExposingService = currentUserIntent.enableExposingService;

      // This kubernetesOperatorVersion is just the generation from the metadata of the CR.
      if (currentUserIntent.kubernetesOperatorVersion != incomingIntent.kubernetesOperatorVersion) {
        // Updating cluster with new userIntent info
        if (!taskParams.updateInProgress) {
          updateListOfPendingTasks(currentUserIntent, incomingIntent);
          Deque<Pair<Field, UserIntent>> deque = pendingTasks.get(currentUserIntent.universeName);
          UserIntent dequeUserIntent = null;
          UserIntent forTaskIntent = null;
          Field field = null;
          Pair<Field, UserIntent> pair = null;
          if (deque != null) { // this should always be non-null
            pair = deque.pop();
            field = pair.getFirst();
            dequeUserIntent = pair.getSecond();
            forTaskIntent = currentUserIntent.clone();
            LOG.info("Field being updated: " + field.getName());
            try {
              field.set(forTaskIntent, field.get(dequeUserIntent));
            } catch (Exception e) {
              LOG.error("Unable to set taskIntent for universe {}", forTaskIntent.universeName, e);
            }
          }

          Cluster primaryCluster = taskParams.getPrimaryCluster();
          primaryCluster.userIntent = incomingIntent;
          taskParams.clusters =
              taskParams.clusters.stream()
                  .filter(c -> !c.clusterType.equals(ClusterType.PRIMARY))
                  .collect(Collectors.toList());
          taskParams.clusters.add(primaryCluster);

          String startingTask =
              String.format("Starting task on universe %s", currentUserIntent.universeName);
          kubernetesStatusUpdater.doKubernetesEventUpdate(
              KubernetesResourceDetails.fromResource(ybUniverse), startingTask);
          if (!incomingIntent.universeOverrides.equals(currentUserIntent.universeOverrides)) {
            LOG.info("Updating Kubernetes Overrides");
            updateOverridesYbUniverse(
                taskParams, cust, ybUniverse, incomingIntent.universeOverrides);
          } else if (!(currentUserIntent.masterGFlags.equals(incomingIntent.masterGFlags))
              || !(currentUserIntent.tserverGFlags.equals(incomingIntent.tserverGFlags))) {
            LOG.info("Updating Gflags");
            updateGflagsYbUniverse(
                taskParams,
                cust,
                ybUniverse,
                incomingIntent.masterGFlags,
                incomingIntent.tserverGFlags);
          } else if (currentUserIntent.numNodes != incomingIntent.numNodes || updateVersion(pair)) {
            LOG.info("Updating nodes");
            updateYBUniverse(taskParams, cust, ybUniverse);
          } else if (!currentUserIntent.ybSoftwareVersion.equals(
              incomingIntent.ybSoftwareVersion)) {
            LOG.info("Upgrading software");
            upgradeYBUniverse(taskParams, cust, ybUniverse);
          } else {
            LOG.info("No update made");
          }
        }
      }
    }
  }

  private boolean updateVersion(Pair<Field, UserIntent> pair) {
    if (pair != null) {
      Field field = pair.getFirst();
      if (field.getName().equals("kubernetesOperatorVersion")) {
        return true;
      }
    }
    return false;
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
      LOG.error("Failed at creating upgrade software params", e);
    }
    requestParams.universeOverrides = universeOverrides;

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);

    UUID taskUUID =
        upgradeUniverseHandler.upgradeKubernetesOverrides(requestParams, cust, oldUniverse);
    LOG.info("Submitted task to upgrade universe overrides with new overrides");
    return taskUUID;
  }

  private UUID updateGflagsYbUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      Map<String, String> masterGflags,
      Map<String, String> tserverGflags) {
    GFlagsUpgradeParams requestParams = new GFlagsUpgradeParams();

    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    try {
      requestParams =
          mapper.readValue(mapper.writeValueAsString(taskParams), GFlagsUpgradeParams.class);
    } catch (Exception e) {
      LOG.error("Failed at creating upgrade software params", e);
    }
    requestParams.masterGFlags = masterGflags;
    requestParams.tserverGFlags = tserverGflags;

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);

    UUID taskUUID = upgradeUniverseHandler.upgradeGFlags(requestParams, cust, oldUniverse);
    LOG.info("Submitted task to upgrade universe with new GFlags");
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
      LOG.error("Failed at creating upgrade software params", e);
    }

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);

    // requestParams.taskType = UpgradeTaskParams.UpgradeTaskType.Software;
    // requestParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    requestParams.ybSoftwareVersion = taskParams.getPrimaryCluster().userIntent.ybSoftwareVersion;
    requestParams.setUniverseUUID(oldUniverse.getUniverseUUID());
    LOG.info("Upgrading universe with new info now");
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
      LOG.error("Failed at creating configure task params for edit", e);
    }

    taskConfigParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.EDIT;
    taskConfigParams.currentClusterType = ClusterType.PRIMARY;
    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), ybUniverse.getMetadata().getName())
            .orElse(null);
    LOG.info("Updating universe with new info now");
    universeCRUDHandler.configure(cust, taskConfigParams);
    return universeCRUDHandler.update(cust, oldUniverse, taskConfigParams);
  }

  private UniverseConfigureTaskParams createTaskParams(YBUniverse ybUniverse, UUID customerUUID)
      throws Exception {
    LOG.info("Creating task params");
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    Cluster cluster = new Cluster(ClusterType.PRIMARY, createUserIntent(ybUniverse, customerUUID));
    taskParams.clusters.add(cluster);
    List<Users> users = Users.getAll(customerUUID);
    if (users.isEmpty()) {
      LOG.error("Users list is of size 0!");
      throw new Exception("Need at least one user");
    } else {
      LOG.info("Taking first user for customer");
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
      LOG.error("Unable to parse universe overrides", e);
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
    LOG.debug("ui.deviceInfo : {}", userIntent.deviceInfo);
    LOG.debug("given deviceInfo: {} ", ybUniverse.getSpec().getDeviceInfo());

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
        LOG.error("could not find ysqlPassword in secret {}", ysqlPassword.getSecretName());
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
        LOG.error("could not find ycqlPassword in secret {}", ycqlPassword.getSecretName());
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
      return kubernetesClient.secrets().inNamespace(namespace.trim()).withName(name).get();
    }
    return kubernetesClient.secrets().inNamespace("default").withName(name).get();
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

  private void addEventHandlersToSharedIndexInformers() {
    ybUniverseInformer.addEventHandler(
        new ResourceEventHandler<YBUniverse>() {
          @Override
          public void onAdd(YBUniverse ybUniverse) {
            LOG.info("YBUniverse {} ADDED", ybUniverse.getMetadata().getName());
            enqueueYBUniverse(ybUniverse, OperatorAction.CREATED);
          }

          @Override
          public void onUpdate(YBUniverse ybUniverse, YBUniverse newYBUniverse) {
            LOG.info("YBUniverse {} MODIFIED", ybUniverse.getMetadata().getName());
            enqueueYBUniverse(newYBUniverse, OperatorAction.UPDATED);
          }

          @Override
          public void onDelete(YBUniverse ybUniverse, boolean b) {
            // Do nothing
            LOG.info("YBUniverse {} DELETED", ybUniverse.getMetadata().getName());
            // reconcile(ybUniverse, OperatorAction.DELETED);
            enqueueYBUniverse(ybUniverse, OperatorAction.DELETED);
          }
        });
  }

  private Provider getProvider(UUID customerUUID, YBUniverse ybUniverse) {
    try {
      // If the provider already exists, don't create another
      String providerName = getProviderName(ybUniverse.getMetadata().getName());
      // Check if need to filter zones.
      List<String> zonesFilter = ybUniverse.getSpec().getZoneFilter();
      List<Provider> providers =
          Provider.getAll(customerUUID).stream()
              .filter(
                  p -> p.getCloudCode() == CloudType.kubernetes && p.getName().equals(providerName))
              .collect(Collectors.toList());
      Provider autoProvider = null;
      if (!providers.isEmpty()) {
        if (!zonesFilter.isEmpty()) {
          LOG.error(
              "Zone filter is not supported with pre-existing providers, ignoring zone filter");
        }
      }

      if (providers.isEmpty() && isRunningInKubernetes()) {
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
        autoProvider =
            cloudProviderHandler.createKubernetes(
                Customer.getOrBadRequest(customerUUID), providerData);
        CloudInfoInterface.mayBeMassageResponse(autoProvider);
      } else {
        LOG.info("Provider is already created, using that...");
        autoProvider = providers.get(0);
      }
      return Provider.getAll(customerUUID).stream()
          .filter(p -> p.getCloudCode() == CloudType.kubernetes && p.getName().equals(providerName))
          .collect(Collectors.toList())
          .get(0);
    } catch (Exception e) {
      if (isRunningInKubernetes()) {
        LOG.error("Running in k8s but no provider created", e);
      } else {
        LOG.info("Not running in k8s");
      }
      LOG.info("No automatic provider found, using first in provider list...");
      return Provider.getAll(customerUUID).stream()
          .filter(p -> p.getCloudCode() == CloudType.kubernetes)
          .collect(Collectors.toList())
          .get(0);
    }
  }

  private void enqueueYBUniverse(YBUniverse ybUniverse, OperatorAction action) {
    LOG.info("enqueueYBUniverse({})", ybUniverse.getMetadata().getName());
    String key = Cache.metaNamespaceKeyFunc(ybUniverse);
    LOG.info("Going to enqueue key {}", key);
    if (key != null && !key.isEmpty()) {
      LOG.info("Adding item to workqueue");
      Pair<String, OperatorAction> pair = new Pair<String, OperatorAction>(key, action);
      workqueue.add(pair);
    }
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

  private void updateListOfPendingTasks(UserIntent oldIntent, UserIntent newIntent) {
    Deque<Pair<Field, UserIntent>> deque = pendingTasks.get(newIntent.universeName);
    if (deque != null) {
      deque.addAll(getDifferences(oldIntent, newIntent));
      pendingTasks.put(oldIntent.universeName, deque);
    } else {
      Deque<Pair<Field, UserIntent>> newDeque = new ArrayDeque<Pair<Field, UserIntent>>();
      newDeque.addAll(getDifferences(oldIntent, newIntent));
      pendingTasks.put(oldIntent.universeName, newDeque);
    }
  }

  private List<Pair<Field, UserIntent>> getDifferences(UserIntent oldIntent, UserIntent newIntent) {
    Field[] fields = newIntent.getClass().getDeclaredFields();
    List<Pair<Field, UserIntent>> differentFields = new ArrayList<>();
    try {
      Pair<Field, UserIntent> kubernetesOperatorVersionUpdate = null;
      for (Field field : fields) {
        int modifiers = field.getModifiers();
        if (!Modifier.isPublic(modifiers)) {
          // ignore private, protected for now;
          continue;
        }
        if (field.get(newIntent) != null && field.get(oldIntent) != null) {
          if (!field.get(oldIntent).equals(field.get(newIntent))) {
            if (!field.getName().equals("kubernetesOperatorVersion")) {
              differentFields.add(new Pair<Field, UserIntent>(field, newIntent));
            } else {
              kubernetesOperatorVersionUpdate = new Pair<Field, UserIntent>(field, newIntent);
            }
          }
        }
      }
      if (kubernetesOperatorVersionUpdate != null) {
        differentFields.add(kubernetesOperatorVersionUpdate);
      }
    } catch (Exception e) {
      LOG.error("Failed to get differences for universe update", e);
    }
    return differentFields;
  }

  private boolean isRunningInKubernetes() {
    String kubernetesServiceHost = System.getenv("KUBERNETES_SERVICE_HOST");
    String kubernetesServicePort = System.getenv("KUBERNETES_SERVICE_PORT");

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
}
