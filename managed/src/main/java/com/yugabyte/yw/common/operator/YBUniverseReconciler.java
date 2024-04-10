// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.AllowedTasks;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
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
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YcqlPassword;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YsqlPassword;
import java.util.Base64;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class YBUniverseReconciler extends AbstractReconciler<YBUniverse> {

  private static final String DELETE_FINALIZER_THREAD_NAME_PREFIX = "universe-delete-finalizer-";

  private final OperatorWorkQueue workqueue;
  private final SharedIndexInformer<YBUniverse> ybUniverseInformer;
  private final String namespace;
  private final Lister<YBUniverse> ybUniverseLister;
  // Resource here has full class name since it conflicts with
  // ybuniversespec.kubernetesoverrides.Resource
  private final MixedOperation<
          YBUniverse,
          KubernetesResourceList<YBUniverse>,
          io.fabric8.kubernetes.client.dsl.Resource<YBUniverse>>
      ybUniverseClient;
  public static final String APP_LABEL = "app";
  private final UniverseCRUDHandler universeCRUDHandler;
  private final UpgradeUniverseHandler upgradeUniverseHandler;
  private final CloudProviderHandler cloudProviderHandler;
  private final TaskExecutor taskExecutor;
  private final RuntimeConfGetter confGetter;
  private final CustomerTaskManager customerTaskManager;
  private final Set<UUID> universeReadySet;
  private final Map<String, String> universeDeletionReferenceMap;
  private final Map<String, UUID> universeTaskMap;
  private Customer customer;
  private OperatorUtils operatorUtils;

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
      KubernetesOperatorStatusUpdater kubernetesStatusUpdater,
      RuntimeConfGetter confGetter,
      CustomerTaskManager customerTaskManager,
      OperatorUtils operatorUtils) {
    this(
        client,
        informerFactory,
        namespace,
        new OperatorWorkQueue(),
        universeCRUDHandler,
        upgradeUniverseHandler,
        cloudProviderHandler,
        taskExecutor,
        kubernetesStatusUpdater,
        confGetter,
        customerTaskManager,
        operatorUtils);
  }

  @VisibleForTesting
  protected YBUniverseReconciler(
      KubernetesClient client,
      YBInformerFactory informerFactory,
      String namespace,
      OperatorWorkQueue workqueue,
      UniverseCRUDHandler universeCRUDHandler,
      UpgradeUniverseHandler upgradeUniverseHandler,
      CloudProviderHandler cloudProviderHandler,
      TaskExecutor taskExecutor,
      KubernetesOperatorStatusUpdater kubernetesStatusUpdater,
      RuntimeConfGetter confGetter,
      CustomerTaskManager customerTaskManager,
      OperatorUtils operatorUtils) {

    super(client, informerFactory);
    this.ybUniverseClient = client.resources(YBUniverse.class);
    this.ybUniverseInformer = informerFactory.getSharedIndexInformer(YBUniverse.class, client);
    this.ybUniverseLister = new Lister<>(ybUniverseInformer.getIndexer());
    this.namespace = namespace;
    this.workqueue = workqueue;
    this.universeCRUDHandler = universeCRUDHandler;
    this.upgradeUniverseHandler = upgradeUniverseHandler;
    this.cloudProviderHandler = cloudProviderHandler;
    this.kubernetesStatusUpdater = kubernetesStatusUpdater;
    this.taskExecutor = taskExecutor;
    this.confGetter = confGetter;
    this.customerTaskManager = customerTaskManager;
    this.ybUniverseInformer.addEventHandler(this);
    this.universeReadySet = ConcurrentHashMap.newKeySet();
    this.universeDeletionReferenceMap = new HashMap<>();
    this.universeTaskMap = new HashMap<>();
    this.operatorUtils = operatorUtils;
  }

  private static String getWorkQueueKey(YBUniverse ybUniverse) {
    String name = ybUniverse.getMetadata().getName();
    String namespace = ybUniverse.getMetadata().getNamespace();
    String uid = ybUniverse.getMetadata().getUid();
    return String.format("%s/%s/%s", namespace, name, uid);
  }

  private static String getListerKeyFromWorkQueueKey(String workQueueKey) {
    String[] splitValues = workQueueKey.split("/");
    String namespace = splitValues[0];
    String name = splitValues[1];
    return String.format("%s/%s", namespace, name);
  }

  private static String getResourceUidFromWorkQueueKey(String workQueueKey) {
    return workQueueKey.split("/")[2];
  }

  @VisibleForTesting
  protected OperatorWorkQueue getOperatorWorkQueue() {
    return workqueue;
  }

  @Override
  public void onAdd(YBUniverse universe) {
    enqueueYBUniverse(universe, OperatorWorkQueue.ResourceAction.CREATE);
  }

  @Override
  public void onUpdate(YBUniverse oldUniverse, YBUniverse newUniverse) {
    // Handle the delete workflow first, as we get a this call before onDelete is called
    if (newUniverse.getMetadata().getDeletionTimestamp() != null) {
      enqueueYBUniverse(newUniverse, OperatorWorkQueue.ResourceAction.DELETE);
      return;
    }
    // Treat this as no-op action enqueue
    enqueueYBUniverse(newUniverse, OperatorWorkQueue.ResourceAction.NO_OP);
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
          log.warn("Invalid resource key: {}", key);
          continue;
        }

        // Get the YBUniverse resource's name
        // from workqueue key which is in format namespace/name/uid.
        // Fisrt get key namespace/name format
        String listerKey = getListerKeyFromWorkQueueKey(key);
        YBUniverse ybUniverse = ybUniverseLister.get(listerKey);
        log.info("Processing YbUniverse name: {}, Object: {}", listerKey, ybUniverse);

        // Handle new calls that can happen from removing the finalizer or any other minor status
        // updates.
        if (ybUniverse == null) {
          if (action == OperatorWorkQueue.ResourceAction.DELETE) {
            log.info("Tried to delete ybUniverse but it's no longer in Lister");
          }
          // Clear any state of the non-existing universe from In-memory maps
          workqueue.clearState(key);
          continue;
        }
        if (ybUniverse.getMetadata().getUid().equals(getResourceUidFromWorkQueueKey(key))) {
          reconcile(ybUniverse, action);
        } else {
          workqueue.clearState(key);
          log.debug("Lister referencing older Universe with same name {}, ignoring", listerKey);
          continue;
        }

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
    String mapKey = getWorkQueueKey(ybUniverse);
    String ybaUniverseName = OperatorUtils.getYbaUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();
    log.info(
        "Reconcile for YbUniverse metadata: Name = {}, Namespace = {}",
        resourceName,
        resourceNamespace);

    try {
      Customer cust = operatorUtils.getOperatorCustomer();
      // checking to see if the universe was deleted.
      if (action == OperatorWorkQueue.ResourceAction.DELETE
          || ybUniverse.getMetadata().getDeletionTimestamp() != null) {
        log.info("deleting universe {}", ybaUniverseName);
        UniverseResp universeResp =
            universeCRUDHandler.findByName(cust, ybaUniverseName).stream().findFirst().orElse(null);

        if (universeResp == null) {
          log.debug("universe {} already deleted in YBA, cleaning up", ybaUniverseName);
          // Check delete finalizer thread does not exist already
          String deleteFinalizerThread = DELETE_FINALIZER_THREAD_NAME_PREFIX + ybaUniverseName;
          // Finalizer remove thread exists if deletion reference map contains resource key
          // and value is equal to the deletion thread name.
          boolean deleteFinalizerThreadExists =
              universeDeletionReferenceMap.containsKey(mapKey)
                  ? (universeDeletionReferenceMap.get(mapKey).equals(deleteFinalizerThread))
                  : false;
          // Add thread to delete provider and remove finalizer
          ObjectMeta objectMeta = ybUniverse.getMetadata();
          if (objectMeta != null
              && CollectionUtils.isNotEmpty(objectMeta.getFinalizers())
              && !deleteFinalizerThreadExists) {
            String dTaskUUIDString = universeDeletionReferenceMap.remove(mapKey);
            universeDeletionReferenceMap.put(mapKey, deleteFinalizerThread);
            UUID customerUUID = cust.getUuid();
            Thread universeDeletionFinalizeThread =
                new Thread(
                    () -> {
                      try {
                        // Wait for deletion task to finish, release In-use provider lock
                        if (dTaskUUIDString != null) {
                          log.debug("Waiting for deletion task to complete...");
                          taskExecutor.waitForTask(UUID.fromString(dTaskUUIDString));
                          log.debug("Deletion task complete");
                        }
                        if (canDeleteProvider(cust, ybaUniverseName)) {
                          try {
                            UUID deleteProviderTaskUUID =
                                deleteProvider(customerUUID, ybaUniverseName);
                            taskExecutor.waitForTask(deleteProviderTaskUUID);
                          } catch (Exception e) {
                            log.error("Got error in deleting provider", e);
                          }
                        }
                        log.info("Removing finalizers...");
                        if (ybUniverse.getMetadata() != null) {
                          objectMeta.setFinalizers(Collections.emptyList());
                          ybUniverseClient
                              .inNamespace(resourceNamespace)
                              .withName(resourceName)
                              .patch(ybUniverse);
                        }
                        universeDeletionReferenceMap.remove(mapKey);
                      } catch (Exception e) {
                        log.info(
                            "Got error in finalizing YbUniverse object name: {}, namespace: {}"
                                + " delete",
                            resourceName,
                            resourceNamespace);
                      }
                    },
                    deleteFinalizerThread);
            universeDeletionFinalizeThread.start();
          }
        } else {
          log.debug("deleting universe {} in yba", ybaUniverseName);
          Universe universe = Universe.getOrBadRequest(universeResp.universeUUID);
          UUID universeUUID = universe.getUniverseUUID();
          universeReadySet.remove(universeUUID);
          universeTaskMap.remove(mapKey);
          workqueue.resetRetries(mapKey);
          // Add check if universe deletion is in progress
          UUID dTaskUUID = deleteUniverse(cust.getUuid(), universeUUID, ybUniverse);
          if (dTaskUUID != null) {
            log.info("Deleted Universe using KubernetesOperator");
            universeDeletionReferenceMap.put(mapKey, dTaskUUID.toString());
            log.info("YBA Universe {} deletion task {} launched", ybaUniverseName, dTaskUUID);
          }
          if (action.equals(OperatorWorkQueue.ResourceAction.NO_OP)) {
            workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false);
          }
        }
      } else if (action == OperatorWorkQueue.ResourceAction.CREATE) {
        createActionReconcile(ybUniverse, cust);
      } else if (action == OperatorWorkQueue.ResourceAction.UPDATE) {
        updateActionReconcile(ybUniverse, cust);
      } else if (action == OperatorWorkQueue.ResourceAction.NO_OP) {
        noOpActionReconcile(ybUniverse, cust);
      }
    } catch (Exception e) {
      log.error("Got Exception in Operator Action", e);
    }
  }

  // CREATE operator action - We typically receive this on 3 occasions:
  // 1. New universe creation
  // 2. YBA Restart - All existing resources receive CREATE calls
  // 3. NO_OP or UPDATE actions sees that the universe is not created - Requeues CREATE
  // Universe creation will be retried until successful
  private void createActionReconcile(YBUniverse ybUniverse, Customer cust) throws Exception {
    String mapKey = getWorkQueueKey(ybUniverse);
    String ybaUniverseName = OperatorUtils.getYbaUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();
    Optional<Universe> uOpt = Universe.maybeGetUniverseByName(cust.getId(), ybaUniverseName);

    if (!uOpt.isPresent()) {
      log.info("Creating new universe {}", ybaUniverseName);
      // Allowing us to update the status of the ybUniverse
      // Setting finalizer to prevent out-of-operator deletes of custom resources
      ObjectMeta objectMeta = ybUniverse.getMetadata();
      objectMeta.setFinalizers(Collections.singletonList("finalizer.k8soperator.yugabyte.com"));
      ybUniverseClient.inNamespace(resourceNamespace).withName(resourceName).patch(ybUniverse);
      UniverseConfigureTaskParams taskParams = createTaskParams(ybUniverse, cust.getUuid());
      Result task = createUniverse(cust.getUuid(), taskParams, ybUniverse);
      log.info("Created Universe KubernetesOperator " + task.toString());
    } else {
      Universe u = uOpt.get();
      UUID pMTaskUUID = u.getUniverseDetails().placementModificationTaskUuid;
      Optional<TaskInfo> oTaskInfo =
          pMTaskUUID != null ? TaskInfo.maybeGet(pMTaskUUID) : Optional.empty();
      if (oTaskInfo.isPresent()) {
        retryOrRerunLastTask(cust.getUuid(), ybUniverse, oTaskInfo.get());
        return;
      }
      State createTaskState = universeCreateTaskState(cust.getUuid(), u.getUniverseUUID());
      if (TaskInfo.ERROR_STATES.contains(createTaskState)) {
        log.debug("Previous attempt to create Universe {} failed, retrying", ybaUniverseName);
        Universe.delete(u.getUniverseUUID());
        UniverseConfigureTaskParams taskParams = createTaskParams(ybUniverse, cust.getUuid());
        createUniverse(cust.getUuid(), taskParams, ybUniverse);
      } else if (createTaskState.equals(State.Success)) {
        // Can receive once on Platform restart
        workqueue.resetRetries(mapKey);
        log.debug("Universe {} already exists, treating as update", ybaUniverseName);
        editUniverse(cust, u, ybUniverse);
      } else {
        log.debug("Universe {}: creation in progress", ybaUniverseName);
      }
    }
  }

  private void updateActionReconcile(YBUniverse ybUniverse, Customer cust) {
    String mapKey = getWorkQueueKey(ybUniverse);
    String ybaUniverseName = OperatorUtils.getYbaUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();
    Optional<Universe> uOpt = Universe.maybeGetUniverseByName(cust.getId(), ybaUniverseName);

    if (!uOpt.isPresent()) {
      log.debug("Update Action: Universe {} creation failed", ybaUniverseName);
      return;
    } else if (uOpt.get().universeIsLocked() || universeTaskInProgress(ybUniverse)) {
      log.debug("Update Action: Universe {} currently locked/task in progress", ybaUniverseName);
      return;
    }

    Universe universe = uOpt.get();
    UUID pMTaskUUID = universe.getUniverseDetails().placementModificationTaskUuid;
    Optional<TaskInfo> oTaskInfo =
        pMTaskUUID != null ? TaskInfo.maybeGet(pMTaskUUID) : Optional.empty();
    if (oTaskInfo.isPresent()) {
      // If previous task failed, retry
      retryOrRerunLastTask(cust.getUuid(), ybUniverse, oTaskInfo.get());
      return;
    }

    State createTaskState = universeCreateTaskState(cust.getUuid(), universe.getUniverseUUID());
    if (TaskInfo.ERROR_STATES.contains(createTaskState)) {
      log.debug("Update Action: Previous attempt to create Universe {} failed", ybaUniverseName);
    } else if (createTaskState.equals(State.Success)) {
      log.debug("Update Action: Universe {} checking for updates", ybaUniverseName);
      workqueue.resetRetries(mapKey);
      editUniverse(cust, universe, ybUniverse);
    } else {
      log.debug("Update Action: Universe {} creation task in progress", ybaUniverseName);
    }
  }

  private void noOpActionReconcile(YBUniverse ybUniverse, Customer cust) {
    String mapKey = getWorkQueueKey(ybUniverse);
    String ybaUniverseName = OperatorUtils.getYbaUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();
    Optional<Universe> uOpt = Universe.maybeGetUniverseByName(cust.getId(), ybaUniverseName);

    if (!uOpt.isPresent()) {
      log.debug("NoOp Action: Universe {} creation failed, requeuing Create", ybaUniverseName);
      workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.CREATE, true);
      return;
    } else if (uOpt.get().universeIsLocked() || universeTaskInProgress(ybUniverse)) {
      log.debug(
          "NoOp Action: Universe {} currently locked/task in progress, requeuing NoOp",
          ybaUniverseName);
      workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false);
      return;
    }

    Universe universe = uOpt.get();
    UUID pMTaskUUID = universe.getUniverseDetails().placementModificationTaskUuid;
    Optional<TaskInfo> oTaskInfo =
        pMTaskUUID != null ? TaskInfo.maybeGet(pMTaskUUID) : Optional.empty();
    if (oTaskInfo.isPresent()) {
      // If previous task failed, requeue Action based on task type
      if (oTaskInfo.get().getTaskType().equals(TaskType.CreateKubernetesUniverse)) {
        log.debug("NoOp Action: Universe {} creation failed, requeuing Create", ybaUniverseName);
        workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.CREATE, true);
      } else {
        log.debug("NoOp Action: Universe {} update failed, requeuing Update", ybaUniverseName);
        workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.UPDATE, true);
      }
      return;
    }

    State createTaskState = universeCreateTaskState(cust.getUuid(), universe.getUniverseUUID());
    if (TaskInfo.ERROR_STATES.contains(createTaskState)) {
      log.debug(
          "NoOp Action: Previous attempt to create Universe {} failed, requeuing Create",
          ybaUniverseName);
      workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.CREATE, true);
    } else if (createTaskState.equals(State.Success)) {
      workqueue.resetRetries(mapKey);
      log.debug(
          "NoOp Action: Universe {} checking for updates, queuing Update if required",
          ybaUniverseName);
      if (operatorUtils.universeAndSpecMismatch(cust, universe, ybUniverse)) {
        workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.UPDATE, false);
      }
    } else {
      log.debug(
          "NoOp Action: Universe {} creation task in progress, requeuing NoOp", ybaUniverseName);
      workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false);
    }
  }

  private State universeCreateTaskState(UUID customerUUID, UUID universeUUID) {
    if (universeReadySet.contains(universeUUID)) {
      return State.Success;
    }
    Optional<CustomerTask> oUniverseCreationCustomerTask =
        CustomerTask.maybeGetByTargetUUIDTaskTypeTargetType(
            customerUUID,
            universeUUID,
            com.yugabyte.yw.models.CustomerTask.TaskType.Create,
            TargetType.Universe);
    Optional<TaskInfo> oUniverseCreationTask =
        oUniverseCreationCustomerTask.isPresent()
            ? TaskInfo.maybeGet(oUniverseCreationCustomerTask.get().getTaskUUID())
            : Optional.empty();
    if (!oUniverseCreationTask.isPresent()
        || oUniverseCreationTask.get().getTaskState().equals(State.Success)) {
      universeReadySet.add(universeUUID);
      return State.Success;
    } else {
      return oUniverseCreationTask.get().getTaskState();
    }
  }

  private boolean universeTaskInProgress(YBUniverse ybUniverse) {
    String mapKey = getWorkQueueKey(ybUniverse);
    UUID currTaskUUID = universeTaskMap.getOrDefault(mapKey, null);
    if (currTaskUUID != null) {
      CustomerTask cTask = CustomerTask.findByTaskUUID(currTaskUUID);
      if (cTask.getCompletionTime() == null) {
        // In-Progress if completion time unset
        return true;
      }
    }
    // If no map entry or completion time set, task is done.
    universeTaskMap.remove(mapKey);
    return false;
  }

  private UUID deleteUniverse(UUID customerUUID, UUID universeUUID, YBUniverse ybUniverse) {
    log.info("Deleting universe using operator");
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    /* customer, universe, isForceDelete, isDeleteBackups, isDeleteAssociatedCerts */
    KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(ybUniverse);
    UUID taskUUID =
        universeCRUDHandler.destroy(customer, universe, true, false, false, resourceDetails);
    return taskUUID;
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

  private void retryOrRerunLastTask(UUID customerUUID, YBUniverse ybUniverse, TaskInfo taskInfo) {
    String ybaUniverseName = OperatorUtils.getYbaUniverseName(ybUniverse);
    Customer cust = Customer.getOrBadRequest(customerUUID);
    UniverseState state =
        taskInfo.getTaskType().equals(TaskType.CreateKubernetesUniverse)
            ? UniverseState.CREATING
            : UniverseState.EDITING;
    kubernetesStatusUpdater.updateUniverseState(
        KubernetesResourceDetails.fromResource(ybUniverse), state);

    UniverseDefinitionTaskParams prevTaskParams =
        Json.fromJson(taskInfo.getTaskParams(), UniverseDefinitionTaskParams.class);
    Universe u = Universe.getOrBadRequest(prevTaskParams.getUniverseUUID());
    TaskType prevTaskType = taskInfo.getTaskType();
    try {
      UUID taskUUID = null;
      AllowedTasks allowedRerunTasks = UniverseTaskBase.getAllowedTasksOnFailure(taskInfo);
      boolean rerunAllowedForTask = allowedRerunTasks.getTaskTypes().contains(prevTaskType);
      boolean shouldRerun =
          rerunAllowedForTask
              && operatorUtils.universeAndSpecMismatch(cust, u, ybUniverse, taskInfo);
      if (shouldRerun) {
        log.debug(
            "Previous {} Universe task failed, rerunning {} with latest params",
            ybaUniverseName,
            taskInfo.getTaskType());
        editUniverse(cust, u, ybUniverse, prevTaskType);
      } else {
        log.debug("Previous {} Universe task failed, retrying", ybaUniverseName);
        CustomerTask cTask =
            customerTaskManager.retryCustomerTask(customerUUID, taskInfo.getTaskUUID());
        universeTaskMap.put(getWorkQueueKey(ybUniverse), cTask.getTaskUUID());
      }
    } catch (Exception e) {
      state =
          taskInfo.getTaskType().equals(TaskType.CreateKubernetesUniverse)
              ? UniverseState.ERROR_CREATING
              : UniverseState.ERROR_UPDATING;
      kubernetesStatusUpdater.updateUniverseState(
          KubernetesResourceDetails.fromResource(ybUniverse), state);
      throw e;
    }
  }

  private Result createUniverse(
      UUID customerUUID, UniverseConfigureTaskParams taskParams, YBUniverse ybUniverse) {
    log.info("creating universe via k8s operator");
    kubernetesStatusUpdater.createYBUniverseEventStatus(
        null,
        KubernetesResourceDetails.fromResource(ybUniverse),
        TaskType.CreateKubernetesUniverse.name());
    kubernetesStatusUpdater.updateUniverseState(
        KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.CREATING);
    try {
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
      universeTaskMap.put(getWorkQueueKey(ybUniverse), universeResp.taskUUID);
      log.info("Done creating universe through CRUD Handler");
      return new YBPTask(universeResp.taskUUID, universeResp.universeUUID).asResult();
    } catch (Exception e) {
      kubernetesStatusUpdater.updateUniverseState(
          KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.ERROR_CREATING);
      throw e;
    }
  }

  @VisibleForTesting
  protected void editUniverse(Customer cust, Universe universe, YBUniverse ybUniverse) {
    editUniverse(cust, universe, ybUniverse, null /* specificTaskTypeToRerun */);
  }

  @VisibleForTesting
  protected void editUniverse(
      Customer cust,
      Universe universe,
      YBUniverse ybUniverse,
      @Nullable TaskType specificTaskTypeToRerun) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (universeDetails == null || universeDetails.getPrimaryCluster() == null) {
      throw new RuntimeException(
          String.format("invalid universe details found for {}", universe.getName()));
    }

    UserIntent currentUserIntent = universeDetails.getPrimaryCluster().userIntent;
    UserIntent incomingIntent = createUserIntent(ybUniverse, cust.getUuid(), false);

    // Fix non-changeable values to current.
    incomingIntent.accessKeyCode = currentUserIntent.accessKeyCode;
    incomingIntent.enableExposingService = currentUserIntent.enableExposingService;

    KubernetesResourceDetails k8ResourceDetails =
        KubernetesResourceDetails.fromResource(ybUniverse);
    UUID taskUUID = null;

    try {
      if (specificTaskTypeToRerun != null) {
        // For cases when we want to do a re-run of same task type
        universeDetails.skipMatchWithUserIntent = true;
        switch (specificTaskTypeToRerun) {
          case EditKubernetesUniverse:
            if (checkAndHandleUniverseLock(
                ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
              return;
            }
            log.info("Re-running Edit Universe with new params");
            kubernetesStatusUpdater.createYBUniverseEventStatus(
                universe, k8ResourceDetails, TaskType.EditKubernetesUniverse.name());
            currentUserIntent.numNodes = incomingIntent.numNodes;
            currentUserIntent.deviceInfo.volumeSize = incomingIntent.deviceInfo.volumeSize;
            taskUUID = updateYBUniverse(universeDetails, cust, ybUniverse);
            break;
          case KubernetesOverridesUpgrade:
            if (checkAndHandleUniverseLock(
                ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
              return;
            }
            log.info("Re-running Kubernetes Overrides with new params");
            kubernetesStatusUpdater.createYBUniverseEventStatus(
                universe, k8ResourceDetails, TaskType.KubernetesOverridesUpgrade.name());
            taskUUID =
                updateOverridesYbUniverse(
                    universeDetails, cust, ybUniverse, incomingIntent.universeOverrides);
            break;
          case GFlagsKubernetesUpgrade:
            if (checkAndHandleUniverseLock(
                ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
              return;
            }
            log.info("Re-running Gflags with new params");
            kubernetesStatusUpdater.createYBUniverseEventStatus(
                universe, k8ResourceDetails, TaskType.GFlagsKubernetesUpgrade.name());
            taskUUID =
                updateGflagsYbUniverse(
                    universeDetails, cust, ybUniverse, incomingIntent.specificGFlags);
            break;
          default:
            log.error("Unexpected task, this should not happen!");
            throw new RuntimeException("Unexpected task tried for re-run");
        }
      } else {
        // Case with new edits
        if (!StringUtils.equals(
            incomingIntent.universeOverrides, currentUserIntent.universeOverrides)) {
          log.info("Updating Kubernetes Overrides");
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.KubernetesOverridesUpgrade.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID =
              updateOverridesYbUniverse(
                  universeDetails, cust, ybUniverse, incomingIntent.universeOverrides);
        } else if (operatorUtils.checkIfGFlagsChanged(
            universe, currentUserIntent.specificGFlags, incomingIntent.specificGFlags)) {
          log.info("Updating Gflags");
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.GFlagsKubernetesUpgrade.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID =
              updateGflagsYbUniverse(
                  universeDetails, cust, ybUniverse, incomingIntent.specificGFlags);
        } else if (!currentUserIntent.ybSoftwareVersion.equals(incomingIntent.ybSoftwareVersion)) {
          log.info("Upgrading software");
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.UpgradeKubernetesUniverse.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID =
              upgradeYBUniverse(
                  universeDetails, cust, ybUniverse, incomingIntent.ybSoftwareVersion);
        } else if (operatorUtils.shouldUpdateYbUniverse(
            currentUserIntent, incomingIntent.numNodes, incomingIntent.deviceInfo)) {
          log.info("Calling Edit Universe");
          currentUserIntent.numNodes = incomingIntent.numNodes;
          currentUserIntent.deviceInfo.volumeSize = incomingIntent.deviceInfo.volumeSize;
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.EditKubernetesUniverse.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID = updateYBUniverse(universeDetails, cust, ybUniverse);
        } else {
          log.info("No update made");
        }
      }
      if (taskUUID != null) {
        universeTaskMap.put(getWorkQueueKey(ybUniverse), taskUUID);
      }
    } catch (Exception e) {
      kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.ERROR_UPDATING);
      throw e;
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
        Universe.maybeGetUniverseByName(cust.getId(), OperatorUtils.getYbaUniverseName(ybUniverse))
            .orElse(null);

    log.info("Upgrade universe overrides with new overrides");
    return upgradeUniverseHandler.upgradeKubernetesOverrides(requestParams, cust, oldUniverse);
  }

  private UUID updateGflagsYbUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      SpecificGFlags newGFlags) {
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
      requestParams.getPrimaryCluster().userIntent.specificGFlags = newGFlags;
    } catch (Exception e) {
      log.error("Failed at creating upgrade software params", e);
    }

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), OperatorUtils.getYbaUniverseName(ybUniverse))
            .orElse(null);

    log.info("Upgrade universe with new GFlags");
    return upgradeUniverseHandler.upgradeGFlags(requestParams, cust, oldUniverse);
  }

  private UUID upgradeYBUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      String newYbSoftwareVersion) {
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    SoftwareUpgradeParams requestParams = new SoftwareUpgradeParams();
    try {
      requestParams =
          mapper.readValue(mapper.writeValueAsString(taskParams), SoftwareUpgradeParams.class);
      requestParams.ybSoftwareVersion = newYbSoftwareVersion;
    } catch (Exception e) {
      log.error("Failed at creating upgrade software params", e);
    }

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), OperatorUtils.getYbaUniverseName(ybUniverse))
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
        Universe.maybeGetUniverseByName(cust.getId(), OperatorUtils.getYbaUniverseName(ybUniverse))
            .orElse(null);
    log.info("Updating universe with new info now");
    universeCRUDHandler.configure(cust, taskConfigParams);
    return universeCRUDHandler.update(cust, oldUniverse, taskConfigParams);
  }

  @VisibleForTesting
  protected UniverseConfigureTaskParams createTaskParams(YBUniverse ybUniverse, UUID customerUUID)
      throws Exception {
    log.info("Creating task params");
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    Cluster cluster =
        new Cluster(ClusterType.PRIMARY, createUserIntent(ybUniverse, customerUUID, true));
    taskParams.clusters.add(cluster);
    List<Users> users = Users.getAll(customerUUID);
    if (users.isEmpty()) {
      log.error("Users list is of size 0!");
      kubernetesStatusUpdater.updateUniverseState(
          KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.ERROR_CREATING);
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

  private UserIntent createUserIntent(YBUniverse ybUniverse, UUID customerUUID, boolean isCreate) {
    try {
      UserIntent userIntent = new UserIntent();
      // Needed for the UI fix because all k8s universes have this now..
      userIntent.dedicatedNodes = true;
      userIntent.universeName = OperatorUtils.getYbaUniverseName(ybUniverse);
      if (ybUniverse.getSpec().getKubernetesOverrides() != null) {
        userIntent.universeOverrides =
            operatorUtils.getKubernetesOverridesString(
                ybUniverse.getSpec().getKubernetesOverrides());
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

      K8SNodeResourceSpec masterResourceSpec = new K8SNodeResourceSpec();
      userIntent.masterK8SNodeResourceSpec = masterResourceSpec;
      K8SNodeResourceSpec tserverResourceSpec = new K8SNodeResourceSpec();
      userIntent.tserverK8SNodeResourceSpec = tserverResourceSpec;

      userIntent.numNodes =
          ybUniverse.getSpec().getNumNodes() != null
              ? ((int) ybUniverse.getSpec().getNumNodes().longValue())
              : 0;
      userIntent.ybSoftwareVersion = ybUniverse.getSpec().getYbSoftwareVersion();
      userIntent.accessKeyCode = "";

      userIntent.deviceInfo = operatorUtils.mapDeviceInfo(ybUniverse.getSpec().getDeviceInfo());
      log.debug("ui.deviceInfo : {}", userIntent.deviceInfo);
      log.debug("given deviceInfo: {} ", ybUniverse.getSpec().getDeviceInfo());

      userIntent.enableYSQL = ybUniverse.getSpec().getEnableYSQL();
      userIntent.enableYCQL = ybUniverse.getSpec().getEnableYCQL();
      userIntent.enableNodeToNodeEncrypt = ybUniverse.getSpec().getEnableNodeToNodeEncrypt();
      userIntent.enableClientToNodeEncrypt = ybUniverse.getSpec().getEnableClientToNodeEncrypt();
      userIntent.kubernetesOperatorVersion = ybUniverse.getMetadata().getGeneration();
      if (ybUniverse.getSpec().getEnableLoadBalancer()) {
        userIntent.enableExposingService = ExposingServiceState.EXPOSED;
      } else {
        userIntent.enableExposingService = ExposingServiceState.UNEXPOSED;
      }

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
      userIntent.specificGFlags = operatorUtils.getGFlagsFromSpec(ybUniverse, provider);
      return userIntent;
    } catch (Exception e) {
      kubernetesStatusUpdater.updateUniverseState(
          KubernetesResourceDetails.fromResource(ybUniverse),
          isCreate ? UniverseState.ERROR_CREATING : UniverseState.ERROR_UPDATING);
      throw e;
    }
  }

  // getSecret find a secret in the namespace an operator is listening on.
  private Secret getSecret(String name) {
    if (StringUtils.isNotBlank(namespace)) {
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
    String storageClass = ybUniverse.getSpec().getDeviceInfo().getStorageClass();
    String kubeNamespace = ybUniverse.getMetadata().getNamespace();
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
                                tempMap.put("STORAGE_CLASS", storageClass);
                                tempMap.put("KUBENAMESPACE", kubeNamespace);
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
    autoProvider.getDetails().getCloudInfo().getKubernetes().setLegacyK8sProvider(false);
    // This is hardcoded so the UI will filter this provider to show in the "managed kubernetes
    // service" tab
    autoProvider.getDetails().getCloudInfo().getKubernetes().setKubernetesProvider("custom");
    // Fetch created provider from DB.
    return Provider.get(customerUUID, providerName, CloudType.kubernetes);
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
      providerName = getProviderName(OperatorUtils.getYbaUniverseName(ybUniverse));
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
    String mapKey = getWorkQueueKey(ybUniverse);
    String listerName = getListerKeyFromWorkQueueKey(mapKey);
    log.debug("Enqueue universe {} with action {}", listerName, action.toString());
    if (action.equals(OperatorWorkQueue.ResourceAction.NO_OP)) {
      workqueue.requeue(mapKey, action, false);
    } else {
      workqueue.add(new Pair<String, OperatorWorkQueue.ResourceAction>(mapKey, action));
      if (action.needsNoOpAction()) {
        workqueue.add(
            new Pair<String, OperatorWorkQueue.ResourceAction>(
                mapKey, OperatorWorkQueue.ResourceAction.NO_OP));
      }
    }
  }

  private boolean isRunningInKubernetes() {
    String kubernetesServiceHost = KubernetesEnvironmentVariables.getServiceHost();
    String kubernetesServicePort = KubernetesEnvironmentVariables.getServicePort();

    return (kubernetesServiceHost != null && kubernetesServicePort != null);
  }

  private String getProviderName(String universeName) {
    return ("prov-" + universeName);
  }

  private boolean checkAndHandleUniverseLock(
      YBUniverse ybUniverse, Universe universe, OperatorWorkQueue.ResourceAction action) {
    log.trace("checking if universe has active tasks");
    // TODO: Is `universeIsLocked()` enough to check here?
    if (universe.universeIsLocked()) {
      log.warn(
          "universe {} is locked, requeue update and try again later",
          OperatorUtils.getYbaUniverseName(ybUniverse));
      workqueue.requeue(getWorkQueueKey(ybUniverse), action, false);
      log.debug("scheduled universe update for requeue");
      return true;
    }
    return false;
  }
}
