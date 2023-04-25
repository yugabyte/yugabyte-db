// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
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
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Cache;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.stream.Collectors;
import org.yb.util.Pair;
import play.Logger;
import play.mvc.Result;

public class KubernetesOperatorController {

  public static final int WORKQUEUE_CAPACITY = 1024;
  // String is key of form namespace/name.
  // OperatorAction is CREATE, EDIT, or DELETE corresponding to CR action.
  private final BlockingQueue<Pair<String, OperatorAction>> workqueue;
  private final SharedIndexInformer<YBUniverse> ybUniverseInformer;
  private Lister<YBUniverse> ybUniverseLister;
  private MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      ybUniverseClient;
  public static final String APP_LABEL = "app";
  private UniverseCRUDHandler universeCRUDHandler;

  public enum OperatorAction {
    CREATED,
    UPDATED,
    DELETED
  }

  public KubernetesOperatorController(
      MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
          ybUniverseClient,
      SharedIndexInformer<YBUniverse> ybUniverseInformer,
      String namespace,
      UniverseCRUDHandler universeCRUDHandler) {
    this.ybUniverseClient = ybUniverseClient;
    this.ybUniverseLister = new Lister<>(ybUniverseInformer.getIndexer(), namespace);
    this.ybUniverseInformer = ybUniverseInformer;
    this.workqueue = new ArrayBlockingQueue<>(WORKQUEUE_CAPACITY);
    this.universeCRUDHandler = universeCRUDHandler;
    addEventHandlersToSharedIndexInformers();
  }

  public void run() {
    Logger.info("Starting YBUniverse controller");
    while (!Thread.currentThread().isInterrupted()) {
      if (ybUniverseInformer.hasSynced()) {
        break;
      }
    }
    // TODO: handle concurrent updates to same resource while work is going on
    while (true) {
      try {
        Logger.info("trying to fetch item from workqueue...");
        if (workqueue.isEmpty()) {
          Logger.info("Work Queue is empty");
        }
        Pair<String, OperatorAction> pair = workqueue.take();
        String key = pair.getFirst();
        OperatorAction action = pair.getSecond();
        Objects.requireNonNull(key, "The workqueue item key can't be null.");
        Logger.info("Got {}", key);
        if ((!key.contains("/"))) {
          Logger.warn("invalid resource key: {}", key);
        }

        // Get the YBUniverse resource's name
        // from key which is in format namespace/name.
        String name = getUniverseName(key);
        YBUniverse ybUniverse = ybUniverseLister.get(name);
        if (ybUniverse == null) {
          if (action == OperatorAction.DELETED) {
            Logger.info("Tried to delete ybUniverse but it's no longer in Lister");
            continue;
          }
          Logger.error("YBUniverse {} in workqueue no longer exists", name);
          return;
        }
        String universeName = ybUniverse.getMetadata().getName();
        KubernetesOperatorStatusUpdater.addToMap(universeName, ybUniverse);
        KubernetesOperatorStatusUpdater.client = ybUniverseClient;
        reconcile(ybUniverse, action);

      } catch (InterruptedException interruptedException) {
        Thread.currentThread().interrupt();
        Logger.error("controller interrupted..");
      } catch (Exception e) {
        Thread.currentThread().interrupt();
        Logger.error("Error in reconcile() of Kubernetes Operator.");
      }
    }
  }

  /**
   * Tries to achieve the desired state for ybUniverse.
   *
   * @param ybUniverse specified ybUniverse
   */
  protected void reconcile(YBUniverse ybUniverse, OperatorAction action) {
    Logger.info("Reached reconcile");
    Logger.info(ybUniverse.getMetadata().getName());

    try {
      List<Customer> custList = Customer.getAll();
      if (custList.size() != 1) {
        throw new Exception("Customer list does not have exactly one customer.");
      }
      Customer cust = custList.get(0);
      // checking to see if the universe was deleted.
      if (ybUniverse.getMetadata().getDeletionTimestamp() != null) {
        KubernetesOperatorStatusUpdater.removeFromMap(ybUniverse.getMetadata().getName());
        Logger.info(ybUniverse.getMetadata().getName());

        UUID universeUUID =
            universeCRUDHandler.findByName(cust, ybUniverse.getMetadata().getName()).stream()
                .findFirst()
                .orElse(null)
                .universeUUID;

        Result task = deleteUniverse(cust.getUuid(), universeUUID);

        // Removing finalizer so we can delete the custom resource.
        ObjectMeta objectMeta = ybUniverse.getMetadata();
        objectMeta.setFinalizers(Collections.emptyList());
        ybUniverseClient
            .inNamespace("default")
            .withName(ybUniverse.getMetadata().getName())
            .patch(ybUniverse);

        Logger.info("Deleted Universe Using KubernetesOperator");
        Logger.info(task.toString());
      } else if (action == OperatorAction.CREATED) {
        // Allowing us to update the status of the ybUniverse

        Logger.info(ybUniverse.getMetadata().toString());
        Logger.info("has been created or edited");
        Logger.info(action.toString());
        UniverseConfigureTaskParams taskParams = createTaskParams(ybUniverse, cust.getUuid());

        // Setting finalizer to prevent out-of-operator deletes of custom resources
        ObjectMeta objectMeta = ybUniverse.getMetadata();
        objectMeta.setFinalizers(Collections.singletonList("finalizer.k8soperator.yugabyte.com"));
        ybUniverseClient
            .inNamespace("default")
            .withName(ybUniverse.getMetadata().getName())
            .patch(ybUniverse);

        if (universeCRUDHandler.findByName(cust, ybUniverse.getMetadata().getName()).isEmpty()) {
          Result task = createUniverse(cust.getUuid(), taskParams);
          Logger.info("Created Universe KubernetesOperator");
          Logger.info(task.toString());
        }
      } else if (action == OperatorAction.UPDATED) {
        Logger.info("Update action - non-delete");
        // TODO: Updating the universe
        String universeName = ybUniverse.getMetadata().getName();
        Optional<Universe> u = Universe.maybeGetUniverseByName(cust.getId(), universeName);
        u.ifPresent(
            universe -> {
              UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
              if (taskParams != null && taskParams.getPrimaryCluster() != null) {
                UserIntent currentUserIntent = taskParams.getPrimaryCluster().userIntent;
                UserIntent incomingIntent = createUserIntent(ybUniverse, cust.getUuid());

                // Updating cluster with new userIntent info
                Cluster primaryCluster = taskParams.getPrimaryCluster();
                primaryCluster.userIntent = incomingIntent;
                taskParams.clusters =
                    taskParams.clusters.stream()
                        .filter(c -> !c.clusterType.equals(ClusterType.PRIMARY))
                        .collect(Collectors.toList());
                taskParams.clusters.add(primaryCluster);

                if (currentUserIntent.kubernetesOperatorVersion
                    != incomingIntent.kubernetesOperatorVersion) {
                  if (!taskParams.updateInProgress) {
                    Logger.info("Updating universe with new info now");
                    universeCRUDHandler.update(cust, universe, taskParams);
                  }
                }
              }
            });
      }
    } catch (Exception e) {
      Logger.error("Got Exception in Operator Action", e);
    }
  }

  private Result deleteUniverse(UUID customerUUID, UUID universeUUID) {
    Logger.info("Deleting universe using operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    /* customer, universe, isForceDelete, isDeleteBackups, isDeleteAssociatedCerts */
    UUID taskUUID = universeCRUDHandler.destroy(customer, universe, false, false, false);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }

  private Result createUniverse(UUID customerUUID, UniverseConfigureTaskParams taskParams) {
    Logger.info("creating universe via k8s operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    taskParams.isKubernetesOperatorControlled = true;
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    taskParams.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, taskParams);

    Logger.info("Done configuring CRUDHandler");

    if (taskParams.clusters.stream()
        .anyMatch(cluster -> cluster.clusterType == ClusterType.ASYNC)) {
      taskParams.currentClusterType = ClusterType.ASYNC;
      universeCRUDHandler.configure(customer, taskParams);
    }

    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    Logger.info("Done creating universe through CRUD Handler");
    return new YBPTask(universeResp.taskUUID, universeResp.universeUUID).asResult();
  }

  private UniverseConfigureTaskParams createTaskParams(YBUniverse ybUniverse, UUID customerUUID) {
    Logger.debug("Creating task params");
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    Cluster cluster = new Cluster(ClusterType.PRIMARY, createUserIntent(ybUniverse, customerUUID));
    taskParams.clusters.add(cluster);
    taskParams.creatingUser = Users.getByEmail("admin");
    // CommonUtils.getUserFromContext(ctx);
    taskParams.expectedUniverseVersion = -1; // -1 skips the version check
    return taskParams;
  }

  private UserIntent createUserIntent(YBUniverse ybUniverse, UUID customerUUID) {
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = ybUniverse.getMetadata().getName();
    Logger.info("Universe name" + userIntent.universeName);

    // TODO: Create provider from info of existing Kubernetes setup
    Provider provider =
        Provider.getAll(customerUUID).stream()
            .filter(p -> p.getCloudCode() == CloudType.kubernetes)
            .collect(Collectors.toList())
            .get(0);

    Logger.info("Provider UUID Is:");
    Logger.info(provider.getUuid().toString());
    userIntent.provider = provider.getUuid().toString();
    // Provider.create(customerUUID, UUID.fromString(userIntent.provider), CloudType.gcp, "gcp-3",
    // details);
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
    // userIntent.useSystemd = useSystemd;
    userIntent.accessKeyCode = "";
    userIntent.assignPublicIP = ybUniverse.getSpec().getAssignPublicIP();
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.volumeSize = 100;
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.storageClass = "standard";
    // userIntent.deviceInfo.storageType = StorageType.Persistent;
    // userIntent.assignStaticPublicIP = assignStaticPublicIP;
    // userIntent.specificGFlags = specificGFlags == null ? null : specificGFlags.clone();
    // userIntent.masterGFlags = new HashMap<>(masterGFlags);
    // userIntent.tserverGFlags = new HashMap<>(tserverGFlags);
    userIntent.useTimeSync = ybUniverse.getSpec().getUseTimeSync();
    userIntent.enableYSQL = ybUniverse.getSpec().getEnableYSQL();
    // userIntent.enableYCQL = enableYCQL;
    // userIntent.enableYSQLAuth = enableYSQLAuth;
    // userIntent.enableYCQLAuth = enableYCQLAuth;
    // userIntent.ysqlPassword = ysqlPassword;
    // userIntent.ycqlPassword = ycqlPassword;
    userIntent.enableYEDIS = ybUniverse.getSpec().getEnableYEDIS();
    userIntent.enableNodeToNodeEncrypt = ybUniverse.getSpec().getEnableNodeToNodeEncrypt();
    userIntent.enableClientToNodeEncrypt = ybUniverse.getSpec().getEnableClientToNodeEncrypt();
    userIntent.kubernetesOperatorVersion = ybUniverse.getMetadata().getGeneration();
    return userIntent;
  }

  private void addEventHandlersToSharedIndexInformers() {
    ybUniverseInformer.addEventHandler(
        new ResourceEventHandler<YBUniverse>() {
          @Override
          public void onAdd(YBUniverse ybUniverse) {
            Logger.info("YBUniverse {} ADDED", ybUniverse.getMetadata().getName());
            enqueueYBUniverse(ybUniverse, OperatorAction.CREATED);
          }

          @Override
          public void onUpdate(YBUniverse ybUniverse, YBUniverse newYBUniverse) {
            Logger.info("YBUniverse {} MODIFIED", ybUniverse.getMetadata().getName());
            enqueueYBUniverse(newYBUniverse, OperatorAction.UPDATED);
          }

          @Override
          public void onDelete(YBUniverse ybUniverse, boolean b) {
            // Do nothing
            Logger.info("YBUniverse {} DELETED", ybUniverse.getMetadata().getName());
            // reconcile(ybUniverse, OperatorAction.DELETED);
            enqueueYBUniverse(ybUniverse, OperatorAction.DELETED);
          }
        });
  }

  private void enqueueYBUniverse(YBUniverse ybUniverse, OperatorAction action) {
    Logger.info("enqueueYBUniverse({})", ybUniverse.getMetadata().getName());
    String key = Cache.metaNamespaceKeyFunc(ybUniverse);
    Logger.info("Going to enqueue key {}", key);
    if (key != null && !key.isEmpty()) {
      Logger.info("Adding item to workqueue");
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
}
