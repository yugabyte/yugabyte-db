// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.google.common.annotations.VisibleForTesting;
import com.nimbusds.oauth2.sdk.util.MapUtils;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.AllowedTasks;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.helpers.OperatorPlacementInfoHelper;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseActionsHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.KubernetesToggleImmutableYbcParams;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse.ThrottleParamValue;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Provider.UsabilityState;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.YBProvider;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YbcThrottleParameters;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YcqlPassword;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YsqlPassword;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
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
// This doesn't support geo partitions as of now.
public class YBUniverseReconciler extends AbstractReconciler<YBUniverse> {

  public static final String YSQL_PASSWORD_SECRET_KEY = "ysqlPassword";
  public static final String YCQL_PASSWORD_SECRET_KEY = "ycqlPassword";

  private static final String DELETE_FINALIZER_THREAD_NAME_PREFIX = "universe-delete-finalizer-";

  private final SharedIndexInformer<Release> releaseInformer;
  public static final String APP_LABEL = "app";
  private final UniverseCRUDHandler universeCRUDHandler;
  private final UpgradeUniverseHandler upgradeUniverseHandler;
  private final CloudProviderHandler cloudProviderHandler;
  private final TaskExecutor taskExecutor;
  private final RuntimeConfGetter confGetter;
  private final CustomerTaskManager customerTaskManager;
  private final UniverseActionsHandler universeActionsHandler;
  private final YbcManager ybcManager;
  private final Set<UUID> universeReadySet;
  private final Map<String, String> universeDeletionReferenceMap;
  private final Map<String, UUID> universeTaskMap;
  // Track auto-provider CRs that are currently being created to avoid duplicate creation calls
  private final Set<String> inProgressAutoProviderCRs;
  private Customer customer;

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
      OperatorUtils operatorUtils,
      UniverseActionsHandler universeActionsHandler,
      YbcManager ybcManager) {
    this(
        client,
        informerFactory,
        namespace,
        new OperatorWorkQueue("Universe" /* resourceType */),
        universeCRUDHandler,
        upgradeUniverseHandler,
        cloudProviderHandler,
        taskExecutor,
        kubernetesStatusUpdater,
        confGetter,
        customerTaskManager,
        operatorUtils,
        universeActionsHandler,
        ybcManager);
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
      OperatorUtils operatorUtils,
      UniverseActionsHandler universeActionsHandler,
      YbcManager ybcManager) {

    super(client, informerFactory, YBUniverse.class, operatorUtils, namespace);
    this.releaseInformer = informerFactory.getSharedIndexInformer(Release.class, client);
    this.universeCRUDHandler = universeCRUDHandler;
    this.upgradeUniverseHandler = upgradeUniverseHandler;
    this.cloudProviderHandler = cloudProviderHandler;
    this.kubernetesStatusUpdater = kubernetesStatusUpdater;
    this.taskExecutor = taskExecutor;
    this.confGetter = confGetter;
    this.customerTaskManager = customerTaskManager;
    this.universeReadySet = ConcurrentHashMap.newKeySet();
    this.universeDeletionReferenceMap = new HashMap<>();
    this.universeTaskMap = new HashMap<>();
    this.inProgressAutoProviderCRs = ConcurrentHashMap.newKeySet();
    this.universeActionsHandler = universeActionsHandler;
    this.ybcManager = ybcManager;
  }

  @VisibleForTesting
  protected OperatorWorkQueue getOperatorWorkQueue() {
    return workqueue;
  }

  @Override
  protected void handleResourceDeletion(
      YBUniverse ybUniverse, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata());
    String ybaUniverseName = getUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();
    log.info("deleting universe {}", ybaUniverseName);
    UniverseResp universeResp;
    if (ybUniverse.getMetadata().getAnnotations() != null
        && ybUniverse
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      universeResp =
          universeCRUDHandler.findByUUID(
              cust,
              UUID.fromString(
                  ybUniverse
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      universeResp =
          universeCRUDHandler.findByName(cust, ybaUniverseName).stream().findFirst().orElse(null);
    }

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
                        Optional<YBProvider> providerOpt =
                            operatorUtils.maybeGetCRForProvider(
                                getProviderName(ybaUniverseName), resourceNamespace);
                        if (providerOpt.isPresent()) {
                          YBProvider provider = providerOpt.get();
                          operatorUtils.checkAndDeleteAutoCreatedProvider(
                              provider, resourceNamespace);
                        } else {
                          UUID deleteProviderTaskUUID =
                              deleteProvider(customerUUID, ybaUniverseName);
                          taskExecutor.waitForTask(deleteProviderTaskUUID);
                        }
                      } catch (Exception e) {
                        log.error("Got error in deleting provider", e);
                      }
                    }
                    // If the release is marked for deletion then remove that as well
                    maybeDeleteRelease(ybUniverse);
                    log.info("Removing finalizers...");
                    operatorUtils.removeFinalizer(ybUniverse, resourceClient);
                  } catch (Exception e) {
                    log.error(
                        "Got error in finalizing YbUniverse object name: {}, namespace: {}"
                            + " delete",
                        resourceName,
                        resourceNamespace,
                        e);
                  } finally {
                    universeDeletionReferenceMap.remove(mapKey);
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
  }

  // CREATE operator action - We typically receive this on 3 occasions:
  // 1. New universe creation
  // 2. YBA Restart - All existing resources receive CREATE calls
  // 3. NO_OP or UPDATE actions sees that the universe is not created - Requeues CREATE
  // Universe creation will be retried until successful
  @Override
  protected void createActionReconcile(YBUniverse ybUniverse, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata());
    String ybaUniverseName = getUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();

    Optional<Universe> uOpt;
    if (ybUniverse.getMetadata().getAnnotations() != null
        && ybUniverse
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      uOpt =
          Universe.maybeGet(
              UUID.fromString(
                  ybUniverse
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      uOpt = Universe.maybeGetUniverseByName(cust.getId(), ybaUniverseName);
    }

    if (!uOpt.isPresent()) {
      log.info("Creating new universe {}", ybaUniverseName);
      // Allowing us to update the status of the ybUniverse
      Provider provider = getProvider(ybUniverse, cust.getUuid());
      if (provider == null) {
        createAutoProvider(ybUniverse, cust.getUuid());
        log.info(
            "Provider not ready, waiting for next NO_OP action for universe {}", ybaUniverseName);
        return;
      }
      ObjectMeta objectMeta = ybUniverse.getMetadata();
      objectMeta.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
      resourceClient.inNamespace(resourceNamespace).withName(resourceName).patch(ybUniverse);
      UniverseConfigureTaskParams taskParams =
          createTaskParams(ybUniverse, cust.getUuid(), provider);
      Result task = createUniverse(cust.getUuid(), taskParams, ybUniverse);
      log.info("Created Universe KubernetesOperator " + task.toString());
    } else {
      Universe u = uOpt.get();
      OperatorUtils.maybeAddYbaResourceId(ybUniverse, u.getUniverseUUID(), resourceClient);
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
        UniverseConfigureTaskParams taskParams =
            createTaskParams(ybUniverse, cust.getUuid(), getProvider(ybUniverse, cust.getUuid()));
        createUniverse(cust.getUuid(), taskParams, ybUniverse);
      } else if (createTaskState.equals(State.Success)) {
        // Can receive once on Platform restart
        // Lets update that the universe is ready in case there are no edits
        // to perform
        kubernetesStatusUpdater.updateYBUniverseStatus(
            u,
            KubernetesResourceDetails.fromResource(ybUniverse),
            "" /* taskName */,
            null /* taskUUID */,
            UniverseState.READY,
            null /* throwable */);
        workqueue.resetRetries(mapKey);
        log.debug("Universe {} already exists, treating as update", ybaUniverseName);
        editUniverse(cust, u, ybUniverse);
      } else {
        log.debug("Universe {}: creation in progress", ybaUniverseName);
      }
    }
  }

  @Override
  protected void updateActionReconcile(YBUniverse ybUniverse, Customer cust) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata());
    String ybaUniverseName = getUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();
    Optional<Universe> uOpt;
    if (ybUniverse.getMetadata().getAnnotations() != null
        && ybUniverse
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      uOpt =
          Universe.maybeGet(
              UUID.fromString(
                  ybUniverse
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      uOpt = Universe.maybeGetUniverseByName(cust.getId(), ybaUniverseName);
    }

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

  @Override
  protected void noOpActionReconcile(YBUniverse ybUniverse, Customer cust) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata());
    String ybaUniverseName = getUniverseName(ybUniverse);
    String resourceName = ybUniverse.getMetadata().getName();
    String resourceNamespace = ybUniverse.getMetadata().getNamespace();
    Optional<Universe> uOpt;
    if (ybUniverse.getMetadata().getAnnotations() != null
        && ybUniverse
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      uOpt =
          Universe.maybeGet(
              UUID.fromString(
                  ybUniverse
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      uOpt = Universe.maybeGetUniverseByName(cust.getId(), ybaUniverseName);
    }

    if (!uOpt.isPresent()) {
      log.debug("NoOp Action: Universe {} not found, requeuing Create", ybaUniverseName);
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
      UserIntent newPrimaryIntent =
          createUserIntent(
              ybUniverse, cust.getUuid(), false, getProvider(ybUniverse, cust.getUuid()));
      UserIntent newReadReplicaIntent = null;
      if (ybUniverse.getSpec().getReadReplica() != null) {
        UserIntent currIntent =
            CollectionUtils.isNotEmpty(universe.getUniverseDetails().getReadOnlyClusters())
                ? universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent
                : newPrimaryIntent;
        newReadReplicaIntent = getReadReplicaUserIntent(currIntent, ybUniverse, cust.getUuid());
      }
      if (operatorUtils.universeAndSpecMismatch(
          cust, universe, ybUniverse, newPrimaryIntent, newReadReplicaIntent)) {
        workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.UPDATE, false);
      }
    } else {
      log.debug(
          "NoOp Action: Universe {} creation task in progress, requeuing NoOp", ybaUniverseName);
      workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false);
    }
  }

  // Helper methods
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
    String mapKey = OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata());
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
    String ybaUniverseName = getUniverseName(ybUniverse);
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
    UserIntent newPrimaryIntent =
        createUserIntent(
            ybUniverse, cust.getUuid(), false, getProvider(ybUniverse, cust.getUuid()));
    UserIntent newReadReplicaIntent = null;
    if (ybUniverse.getSpec().getReadReplica() != null) {
      UserIntent currIntent =
          CollectionUtils.isNotEmpty(u.getUniverseDetails().getReadOnlyClusters())
              ? u.getUniverseDetails().getReadOnlyClusters().get(0).userIntent
              : newPrimaryIntent;
      newReadReplicaIntent = getReadReplicaUserIntent(currIntent, ybUniverse, cust.getUuid());
    }
    TaskType prevTaskType = taskInfo.getTaskType();
    try {
      UUID taskUUID = null;
      AllowedTasks allowedRerunTasks = UniverseTaskBase.getAllowedTasksOnFailure(taskInfo);
      boolean rerunAllowedForTask = allowedRerunTasks.getTaskTypes().contains(prevTaskType);
      boolean shouldRerun =
          rerunAllowedForTask
              && operatorUtils.universeAndSpecMismatch(
                  cust, u, ybUniverse, newPrimaryIntent, newReadReplicaIntent, taskInfo);
      if (shouldRerun) {
        log.debug(
            "Previous {} Universe task failed, rerunning {} with latest params",
            ybaUniverseName,
            taskInfo.getTaskType());
        editUniverse(cust, u, ybUniverse, prevTaskType);
      } else {
        log.debug("Previous {} Universe task failed, retrying", ybaUniverseName);
        CustomerTask cTask =
            customerTaskManager.retryCustomerTask(customerUUID, taskInfo.getUuid());
        universeTaskMap.put(
            OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata()), cTask.getTaskUUID());
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

      // CRUDHandler intentionally skips userIntentOverrides handling for operator-controlled
      // universes; compute them here using the operator-specific perAZ-or-fallback rules.
      applyKubernetesOperatorVolumeOverrides(
          taskParams, ybUniverse, customerUUID, null /* existingUniverse */);

      UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
      universeTaskMap.put(
          OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata()), universeResp.taskUUID);
      OperatorUtils.maybeAddYbaResourceId(ybUniverse, universeResp.universeUUID, resourceClient);
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
    UserIntent incomingIntent =
        createUserIntent(
            ybUniverse, cust.getUuid(), false, getProvider(ybUniverse, cust.getUuid()));

    // Handle previously unset masterDeviceInfo
    if (currentUserIntent.masterDeviceInfo == null) {
      currentUserIntent.masterDeviceInfo = operatorUtils.defaultMasterDeviceInfo();
    }

    // Fix non-changeable values to current.
    incomingIntent.accessKeyCode = currentUserIntent.accessKeyCode;
    incomingIntent.enableExposingService = currentUserIntent.enableExposingService;

    UserIntent incomingReadReplicaIntent = null;
    if (ybUniverse.getSpec().getReadReplica() != null) {
      UserIntent currIntent =
          CollectionUtils.isNotEmpty(universe.getUniverseDetails().getReadOnlyClusters())
              ? universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent
              : incomingIntent;
      incomingReadReplicaIntent = getReadReplicaUserIntent(currIntent, ybUniverse, cust.getUuid());
    }

    KubernetesResourceDetails k8ResourceDetails =
        KubernetesResourceDetails.fromResource(ybUniverse);

    // If the universe is paused, and we aren't unpausing, requeue the task.
    if (ybUniverse.getSpec().getPaused() && universeDetails.universePaused) {
      log.debug("Universe {} is paused, requeuing", ybUniverse.getMetadata().getName());
      workqueue.requeue(
          OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata()),
          OperatorWorkQueue.ResourceAction.NO_OP,
          false);
    }

    int sleepAfterEachPodRestart =
        confGetter.getConfForScope(universe, UniverseConfKeys.rollingOpsWaitAfterEachPodMs);
    universeDetails.sleepAfterMasterRestartMillis = sleepAfterEachPodRestart;
    universeDetails.sleepAfterTServerRestartMillis = sleepAfterEachPodRestart;

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
            if (ybUniverse.getSpec().getTserverVolume() != null
                || ybUniverse.getSpec().getMasterVolume() != null) {
              currentUserIntent.deviceInfo = incomingIntent.deviceInfo;
              currentUserIntent.masterDeviceInfo = incomingIntent.masterDeviceInfo;
              if (ybUniverse.getSpec().getTserverVolume() != null
                  && ybUniverse.getSpec().getTserverVolume().getPerAZ() != null) {
                currentUserIntent.updateAZVolumeOverrides(
                    incomingIntent,
                    universeDetails.getPrimaryCluster().placementInfo.getAllAZUUIDs(),
                    null,
                    false /* isDedicatedMaster */);
              }
              if (ybUniverse.getSpec().getMasterVolume() != null
                  && ybUniverse.getSpec().getMasterVolume().getPerAZ() != null) {
                currentUserIntent.updateAZVolumeOverrides(
                    incomingIntent,
                    universeDetails.getPrimaryCluster().placementInfo.getAllAZUUIDs(),
                    null,
                    true /* isDedicatedMaster */);
              }
            } else {
              currentUserIntent.deviceInfo.volumeSize = incomingIntent.deviceInfo.volumeSize;
              currentUserIntent.masterDeviceInfo.volumeSize =
                  incomingIntent.masterDeviceInfo.volumeSize;
            }
            currentUserIntent.masterK8SNodeResourceSpec = incomingIntent.masterK8SNodeResourceSpec;
            currentUserIntent.tserverK8SNodeResourceSpec =
                incomingIntent.tserverK8SNodeResourceSpec;
            // Update the placement info in the task params
            if (ybUniverse.getSpec().getPlacementInfo() != null) {
              universeDetails.getPrimaryCluster().placementInfo =
                  createPlacementInfo(
                      ybUniverse,
                      cust.getUuid(),
                      universeDetails.getPrimaryCluster().placementInfo);
              universeDetails.userAZSelected = true;
            }
            taskUUID = updateYBUniverse(universeDetails, cust, ybUniverse, ClusterType.PRIMARY);
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
                    universeDetails,
                    cust,
                    ybUniverse,
                    incomingIntent.universeOverrides,
                    true /* isRerun */);
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
                    universeDetails,
                    cust,
                    ybUniverse,
                    incomingIntent.specificGFlags,
                    true /* isRerun */);
            break;
          default:
            log.error("Unexpected task, this should not happen!");
            throw new RuntimeException("Unexpected task tried for re-run");
        }
      } else {
        // Handle Pause
        if (!universeDetails.universePaused && ybUniverse.getSpec().getPaused()) {
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.PauseUniverse.name());
          taskUUID =
              universeActionsHandler.pause(
                  cust, universe, KubernetesResourceDetails.fromResource(ybUniverse));
          // Handle Resume
        } else if (universeDetails.universePaused && !ybUniverse.getSpec().getPaused()) {
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.ResumeUniverse.name());
          taskUUID = resumeYbUniverse(cust, universe, ybUniverse);
          // Rare case where creating the Resume task will raise IOException and return null.
          // Handle and requeue.
          if (taskUUID == null) {
            log.error("failed to create universe resume task");
            kubernetesStatusUpdater.updateUniverseState(
                KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.ERROR_UPDATING);
            workqueue.requeue(
                OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata()),
                OperatorWorkQueue.ResourceAction.NO_OP,
                false);
            return;
          }
          // Handle updating throttle params.
        } else if (operatorUtils.isThrottleParamUpdate(universe, ybUniverse)) {
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          // TODO: We should probably update the universe status better here, but the
          // KubernetesOperatorStatusUpdater currently doesn't have a good way to do this - all
          // action updates are task based right now
          updateThrottleParams(universe, ybUniverse);
          kubernetesStatusUpdater.updateUniverseState(
              KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.READY);
          // Handle immutable YBC (useYbdbInbuiltYbc) toggle
        } else if (currentUserIntent.isUseYbdbInbuiltYbc()
            != ybUniverse.getSpec().getUseYbdbInbuiltYbc()) {
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.KubernetesToggleImmutableYbc.name());
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID =
              toggleYbcYbUniverse(
                  universeDetails, cust, ybUniverse, ybUniverse.getSpec().getUseYbdbInbuiltYbc());
          // Case with new edits
        } else if (!HelmUtils.equal(
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
                  universeDetails,
                  cust,
                  ybUniverse,
                  incomingIntent.universeOverrides,
                  false /* isRerun */);
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
                  universeDetails,
                  cust,
                  ybUniverse,
                  incomingIntent.specificGFlags,
                  false /* isRerun */);
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
          // Handle primary cluster edits
        } else if (operatorUtils.shouldUpdatePrimaryCluster(
            universeDetails.getPrimaryCluster(), ybUniverse, incomingIntent)) {
          log.info("Calling Edit Universe");
          currentUserIntent.numNodes = incomingIntent.numNodes;
          if (ybUniverse.getSpec().getTserverVolume() != null
              || ybUniverse.getSpec().getMasterVolume() != null) {
            currentUserIntent.deviceInfo = incomingIntent.deviceInfo;
            currentUserIntent.masterDeviceInfo = incomingIntent.masterDeviceInfo;
            if (ybUniverse.getSpec().getTserverVolume() != null
                && ybUniverse.getSpec().getTserverVolume().getPerAZ() != null) {
              currentUserIntent.updateAZVolumeOverrides(
                  incomingIntent,
                  universeDetails.getPrimaryCluster().placementInfo.getAllAZUUIDs(),
                  null,
                  false /* isDedicatedMaster */);
            }
            if (ybUniverse.getSpec().getMasterVolume() != null
                && ybUniverse.getSpec().getMasterVolume().getPerAZ() != null) {
              currentUserIntent.updateAZVolumeOverrides(
                  incomingIntent,
                  universeDetails.getPrimaryCluster().placementInfo.getAllAZUUIDs(),
                  null,
                  true /* isDedicatedMaster */);
            }
          } else {
            currentUserIntent.deviceInfo.volumeSize = incomingIntent.deviceInfo.volumeSize;
            currentUserIntent.masterDeviceInfo.volumeSize =
                incomingIntent.masterDeviceInfo.volumeSize;
          }
          currentUserIntent.masterK8SNodeResourceSpec = incomingIntent.masterK8SNodeResourceSpec;
          currentUserIntent.tserverK8SNodeResourceSpec = incomingIntent.tserverK8SNodeResourceSpec;
          // Update the placement info in the task params
          if (ybUniverse.getSpec().getPlacementInfo() != null) {
            universeDetails.getPrimaryCluster().placementInfo =
                createPlacementInfo(
                    ybUniverse, cust.getUuid(), universeDetails.getPrimaryCluster().placementInfo);
            universeDetails.userAZSelected = true;
          }
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.EditKubernetesUniverse.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID = updateYBUniverse(universeDetails, cust, ybUniverse, ClusterType.PRIMARY);
          // Handle read replica cluster edits
        } else if (operatorUtils.shouldAddReadReplica(universe, ybUniverse)) {
          log.info("Adding Read Replica");
          addReadReplicaClusterToUniverseDetails(universeDetails, ybUniverse, cust.getUuid());
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.ReadOnlyKubernetesClusterCreate.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);

          taskUUID = addReadReplicaCluster(cust, universeDetails, ybUniverse);
        } else if (operatorUtils.shouldUpdateReadReplica(
            universe, ybUniverse, incomingReadReplicaIntent)) {
          log.info("Updating Read Replica");
          updateReadReplicaClusterInUniverseDetails(
              universeDetails, ybUniverse, cust.getUuid(), incomingReadReplicaIntent);
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.EditKubernetesUniverse.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID = updateYBUniverse(universeDetails, cust, ybUniverse, ClusterType.ASYNC);
        } else if (operatorUtils.shouldRemoveReadReplica(universe, ybUniverse)) {
          log.info("Removing Read Replica");
          UUID clusterUUID = universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid;
          kubernetesStatusUpdater.createYBUniverseEventStatus(
              universe, k8ResourceDetails, TaskType.ReadOnlyKubernetesClusterDelete.name());
          if (checkAndHandleUniverseLock(
              ybUniverse, universe, OperatorWorkQueue.ResourceAction.NO_OP)) {
            return;
          }
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.EDITING);
          taskUUID =
              universeCRUDHandler.clusterDelete(
                  cust, universe, clusterUUID, true, k8ResourceDetails);
        } else {
          log.info("No update made");
          kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.READY);
        }
      }
      if (taskUUID != null) {
        universeTaskMap.put(OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata()), taskUUID);
      }
    } catch (Exception e) {
      kubernetesStatusUpdater.updateUniverseState(k8ResourceDetails, UniverseState.ERROR_UPDATING);
      throw e;
    }
  }

  private UUID toggleYbcYbUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      boolean useYbdbInbuiltYbc) {
    KubernetesToggleImmutableYbcParams requestParams = new KubernetesToggleImmutableYbcParams();
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    try {
      requestParams =
          mapper.readValue(
              mapper.writeValueAsString(taskParams), KubernetesToggleImmutableYbcParams.class);
    } catch (Exception e) {
      log.error("Failed at creating toggle immutable YBC params", e);
    }
    requestParams.setUseYbdbInbuiltYbc(useYbdbInbuiltYbc);

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), getUniverseName(ybUniverse)).orElse(null);
    log.info("Toggling YBC useYbdbInbuiltYbc to {}", useYbdbInbuiltYbc);
    requestParams.setUniverseUUID(oldUniverse.getUniverseUUID());
    return upgradeUniverseHandler.kubernetesToggleImmutableYbc(requestParams, cust, oldUniverse);
  }

  private UUID updateOverridesYbUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      String universeOverrides,
      boolean isRerun) {
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
    requestParams.skipNodeChecks = isRerun;

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), getUniverseName(ybUniverse)).orElse(null);

    log.info("Upgrade universe overrides with new overrides");
    return upgradeUniverseHandler.upgradeKubernetesOverrides(requestParams, cust, oldUniverse);
  }

  private UUID updateGflagsYbUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      SpecificGFlags newGFlags,
      boolean isRerun) {
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
    requestParams.skipNodeChecks = isRerun;

    Universe oldUniverse =
        Universe.maybeGetUniverseByName(cust.getId(), getUniverseName(ybUniverse)).orElse(null);

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
        Universe.maybeGetUniverseByName(cust.getId(), getUniverseName(ybUniverse)).orElse(null);

    requestParams.setUniverseUUID(oldUniverse.getUniverseUUID());
    log.info("Upgrading universe with new info now");
    return upgradeUniverseHandler.upgradeSoftware(requestParams, cust, oldUniverse);
  }

  private UUID updateYBUniverse(
      UniverseDefinitionTaskParams taskParams,
      Customer cust,
      YBUniverse ybUniverse,
      ClusterType clusterType) {
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
    taskConfigParams.currentClusterType = clusterType;
    taskConfigParams.isKubernetesOperatorControlled = true;
    Universe oldUniverse;
    if (ybUniverse.getMetadata().getAnnotations() != null
        && ybUniverse
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      oldUniverse =
          Universe.maybeGet(
                  UUID.fromString(
                      ybUniverse
                          .getMetadata()
                          .getAnnotations()
                          .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)))
              .orElse(null);
    } else {
      oldUniverse =
          Universe.maybeGetUniverseByName(cust.getId(), getUniverseName(ybUniverse)).orElse(null);
    }
    log.info("Updating universe with new info now");
    universeCRUDHandler.configure(cust, taskConfigParams);
    // CRUDHandler skips userIntentOverrides handling for operator-controlled universes; recompute
    // them here using the operator-specific perAZ-or-fallback rules before submitting the edit.
    applyKubernetesOperatorVolumeOverrides(
        taskConfigParams, ybUniverse, cust.getUuid(), oldUniverse);
    return universeCRUDHandler.update(cust, oldUniverse, taskConfigParams);
  }

  private UUID resumeYbUniverse(Customer cust, Universe universe, YBUniverse ybUniverse) {
    try {
      return universeActionsHandler.resume(
          cust, universe, KubernetesResourceDetails.fromResource(ybUniverse));
    } catch (IOException e) {
      log.error("Failed to resume universe", e);
      return null;
    }
  }

  @VisibleForTesting
  protected UniverseConfigureTaskParams createTaskParams(
      YBUniverse ybUniverse, UUID customerUUID, Provider provider) throws Exception {
    log.info("Creating task params");
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    UserIntent primaryUserIntent = createUserIntent(ybUniverse, customerUUID, true, provider);
    Cluster cluster = new Cluster(ClusterType.PRIMARY, primaryUserIntent);
    if (ybUniverse.getSpec().getPlacementInfo() != null) {
      try {
        cluster.placementInfo = createPlacementInfo(ybUniverse, customerUUID, null);
        taskParams.userAZSelected = true;
      } catch (Exception e) {
        log.error("Invalid placement info: {}", e.getMessage(), e);
        throw new RuntimeException("Invalid placement info: " + e.getMessage(), e);
      }
    }
    taskParams.clusters.add(cluster);
    if (ybUniverse.getSpec().getReadReplica() != null) {
      addReadReplicaClusterToUniverseDetails(taskParams, ybUniverse, customerUUID);
    }
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

    // Handle rootCA certificate
    String rootCAName = ybUniverse.getSpec().getRootCA();
    if (rootCAName != null && !rootCAName.trim().isEmpty()) {
      try {
        // Look for certificate by its original name (not universe-specific label)
        CertificateInfo rootCACert = CertificateInfo.get(customerUUID, rootCAName);
        if (rootCACert != null) {
          taskParams.rootCA = rootCACert.getUuid();
          log.info("Found rootCA certificate '{}' with UUID: {}", rootCAName, rootCACert.getUuid());
        } else {
          log.error("RootCA certificate '{}' not found for customer {}", rootCAName, customerUUID);
          kubernetesStatusUpdater.updateUniverseState(
              KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.ERROR_CREATING);
          throw new RuntimeException("RootCA certificate '" + rootCAName + "' not found");
        }
      } catch (Exception e) {
        log.error("Error looking up rootCA certificate '{}': {}", rootCAName, e.getMessage());
        kubernetesStatusUpdater.updateUniverseState(
            KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.ERROR_CREATING);
        throw new RuntimeException(
            "Error looking up rootCA certificate '" + rootCAName + "': " + e.getMessage());
      }
    }

    return taskParams;
  }

  @VisibleForTesting
  protected UniverseConfigureTaskParams createTaskParams(YBUniverse ybUniverse, UUID customerUUID)
      throws Exception {
    Provider provider = getProvider(ybUniverse, customerUUID);
    return createTaskParams(ybUniverse, customerUUID, provider);
  }

  private UserIntent createUserIntent(
      YBUniverse ybUniverse, UUID customerUUID, boolean isCreate, Provider provider) {
    Optional<Universe> optUniverse = Optional.empty();
    if (!isCreate) {
      Customer cust = Customer.getOrBadRequest(customerUUID);
      optUniverse = Universe.maybeGetUniverseByName(cust.getId(), getUniverseName(ybUniverse));
    }
    try {
      UserIntent userIntent = new UserIntent();
      // Needed for the UI fix because all k8s universes have this now..
      userIntent.dedicatedNodes = true;
      userIntent.universeName = getUniverseName(ybUniverse);
      if (ybUniverse.getSpec().getKubernetesOverrides() != null) {
        userIntent.universeOverrides =
            operatorUtils.getKubernetesOverridesString(
                ybUniverse.getSpec().getKubernetesOverrides());
      }
      if (provider == null || provider.getUsabilityState() != UsabilityState.READY) {
        log.error("Provider {} is not ready", provider.getName());
        throw new RuntimeException("Provider " + provider.getName() + " is not ready");
      }
      userIntent.provider = provider.getUuid().toString();
      userIntent.providerType = CloudType.kubernetes;
      userIntent.replicationFactor =
          ybUniverse.getSpec().getReplicationFactor() != null
              ? ((int) ybUniverse.getSpec().getReplicationFactor().longValue())
              : 0;
      userIntent.regionList =
          provider.getRegions().stream().map(r -> r.getUuid()).collect(Collectors.toList());

      // Its possible for the master or tserver resource spec to not be defined in the CRD when the
      // customer has not yet updated the crd. In that case, we want to ensure the user intent will
      // fallback to the actual resource spec from the created universe in PG.
      // OTHERWISE: it is possible for an edit universe task to trigger which can change the
      // resources used by the pods.
      if (ybUniverse.getSpec().getMasterResourceSpec() != null || !optUniverse.isPresent()) {
        userIntent.masterK8SNodeResourceSpec =
            operatorUtils.toNodeResourceSpec(
                ybUniverse.getSpec().getMasterResourceSpec(), s -> s.getCpu(), s -> s.getMemory());
      } else {
        userIntent.masterK8SNodeResourceSpec =
            optUniverse
                .get()
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .masterK8SNodeResourceSpec;
      }

      if (ybUniverse.getSpec().getTserverResourceSpec() != null || !optUniverse.isPresent()) {
        userIntent.tserverK8SNodeResourceSpec =
            operatorUtils.toNodeResourceSpec(
                ybUniverse.getSpec().getTserverResourceSpec(), s -> s.getCpu(), s -> s.getMemory());
      } else {
        userIntent.tserverK8SNodeResourceSpec =
            optUniverse
                .get()
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .tserverK8SNodeResourceSpec;
      }

      userIntent.numNodes =
          ybUniverse.getSpec().getNumNodes() != null
              ? ((int) ybUniverse.getSpec().getNumNodes().longValue())
              : 0;
      userIntent.ybSoftwareVersion = ybUniverse.getSpec().getYbSoftwareVersion();
      userIntent.accessKeyCode = "";

      // Use new volume fields if any are present, otherwise fall back to old deviceInfo fields
      // If tserverVolume or masterVolume is present, use new fields for both (mutual exclusivity)
      if (ybUniverse.getSpec().getTserverVolume() != null
          || ybUniverse.getSpec().getMasterVolume() != null) {
        // Use new volume fields
        userIntent.deviceInfo =
            operatorUtils.mapTserverVolume(ybUniverse.getSpec().getTserverVolume());
        userIntent.masterDeviceInfo =
            operatorUtils.mapMasterVolume(ybUniverse.getSpec().getMasterVolume());
      } else {
        // Use old deviceInfo fields
        userIntent.deviceInfo = operatorUtils.mapDeviceInfo(ybUniverse.getSpec().getDeviceInfo());
        userIntent.masterDeviceInfo =
            operatorUtils.mapMasterDeviceInfo(ybUniverse.getSpec().getMasterDeviceInfo());
      }
      if (userIntent.deviceInfo == null) {
        userIntent.deviceInfo = operatorUtils.defaultDeviceInfo();
      }
      if (userIntent.masterDeviceInfo == null) {
        userIntent.masterDeviceInfo = operatorUtils.defaultMasterDeviceInfo();
      }

      userIntent.enableYSQL = ybUniverse.getSpec().getEnableYSQL();
      userIntent.enableYCQL = ybUniverse.getSpec().getEnableYCQL();
      userIntent.enableYEDIS = false; // Always disable YEDIS
      userIntent.enableNodeToNodeEncrypt = ybUniverse.getSpec().getEnableNodeToNodeEncrypt();
      userIntent.enableClientToNodeEncrypt = ybUniverse.getSpec().getEnableClientToNodeEncrypt();
      userIntent.setUseYbdbInbuiltYbc(
          Boolean.TRUE.equals(ybUniverse.getSpec().getUseYbdbInbuiltYbc()));
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
        resourceTracker.trackDependency(
            currentReconcileResource, ysqlSecret, currentLocalInstanceUuid);
        log.trace(
            "Tracking secret {} as dependency of {}",
            ysqlSecret.getMetadata().getName(),
            currentReconcileResource);
        String password = parseSecretForKey(ysqlSecret, YSQL_PASSWORD_SECRET_KEY);
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
        resourceTracker.trackDependency(
            currentReconcileResource, ycqlSecret, currentLocalInstanceUuid);
        log.trace(
            "Tracking secret {} as dependency of {}",
            ycqlSecret.getMetadata().getName(),
            currentReconcileResource);
        String password = parseSecretForKey(ycqlSecret, YCQL_PASSWORD_SECRET_KEY);
        if (password == null) {
          log.error("could not find ycqlPassword in secret {}", ycqlPassword.getSecretName());
          throw new RuntimeException(
              "could not find ycqlPassword in secret " + ycqlPassword.getSecretName());
        }
        userIntent.enableYCQLAuth = true;
        userIntent.ycqlPassword = password;
      }
      userIntent.specificGFlags = operatorUtils.getGFlagsFromSpec(ybUniverse, provider);

      // Handle AZ deviceInfo overrides and perAZ from new volume fields
      if ((ybUniverse.getSpec().getTserverVolume() != null
              && ybUniverse.getSpec().getTserverVolume().getPerAZ() != null)
          || (ybUniverse.getSpec().getMasterVolume() != null
              && ybUniverse.getSpec().getMasterVolume().getPerAZ() != null)) {
        // Use new volume fields - perAZ is handled within the volume fields
        applyPrimaryClusterUserIntentOverrides(
            provider,
            ybUniverse.getSpec().getTserverVolume(),
            ybUniverse.getSpec().getMasterVolume(),
            userIntent);
      }

      return userIntent;
    } catch (Exception e) {
      kubernetesStatusUpdater.updateUniverseState(
          KubernetesResourceDetails.fromResource(ybUniverse),
          isCreate ? UniverseState.ERROR_CREATING : UniverseState.ERROR_UPDATING);
      throw e;
    }
  }

  private PlacementInfo createPlacementInfo(
      YBUniverse ybUniverse, UUID customerUUID, @Nullable PlacementInfo existingPlacementInfo) {
    return createPlacementInfo(ybUniverse, customerUUID, false, existingPlacementInfo);
  }

  private PlacementInfo createPlacementInfo(
      YBUniverse ybUniverse,
      UUID customerUUID,
      boolean isReadOnlyCluster,
      @Nullable PlacementInfo existingPlacementInfo) {
    Provider provider = getProvider(ybUniverse, customerUUID);
    PlacementInfo placementInfo;

    if (isReadOnlyCluster) {
      placementInfo =
          OperatorPlacementInfoHelper.createPlacementInfo(
              ybUniverse.getSpec().getReadReplica().getPlacementInfo(),
              provider,
              existingPlacementInfo);
      OperatorPlacementInfoHelper.verifyPlacementInfo(
          placementInfo, ybUniverse.getSpec().getReadReplica().getNumNodes().intValue());
    } else {
      placementInfo =
          OperatorPlacementInfoHelper.createPlacementInfo(
              ybUniverse.getSpec().getPlacementInfo(), provider, existingPlacementInfo);
      OperatorPlacementInfoHelper.verifyPlacementInfo(
          placementInfo, ybUniverse.getSpec().getNumNodes().intValue());
    }

    return placementInfo;
  }

  private void updateThrottleParams(Universe universe, YBUniverse ybUniverse) {
    YbcThrottleParameters specParams = ybUniverse.getSpec().getYbcThrottleParameters();
    Map<String, ThrottleParamValue> currentParamsMap =
        ybcManager.getThrottleParams(universe.getUniverseUUID()).getThrottleParamsMap();
    if (specParams == null) {
      // create default spec params
      specParams = new YbcThrottleParameters();
    }
    if (specParams.getMaxConcurrentDownloads() == null)
      specParams.setMaxConcurrentDownloads(
          (long)
              currentParamsMap
                  .get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS)
                  .getPresetValues()
                  .getDefaultValue());
    if (specParams.getMaxConcurrentUploads() == null)
      specParams.setMaxConcurrentUploads(
          (long)
              currentParamsMap
                  .get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS)
                  .getPresetValues()
                  .getDefaultValue());
    if (specParams.getPerDownloadNumObjects() == null)
      specParams.setPerDownloadNumObjects(
          (long)
              currentParamsMap
                  .get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS)
                  .getPresetValues()
                  .getDefaultValue());
    if (specParams.getPerUploadNumObjects() == null)
      specParams.setPerUploadNumObjects(
          (long)
              currentParamsMap
                  .get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS)
                  .getPresetValues()
                  .getDefaultValue());
    if (specParams.getDiskReadBytesPerSec() == null)
      specParams.setDiskReadBytesPerSec(
          (long)
              currentParamsMap
                  .get(GFlagsUtil.YBC_DISK_READ_BYTES_PER_SECOND)
                  .getPresetValues()
                  .getDefaultValue());
    if (specParams.getDiskWriteBytesPerSec() == null)
      specParams.setDiskWriteBytesPerSec(
          (long)
              currentParamsMap
                  .get(GFlagsUtil.YBC_DISK_WRITE_BYTES_PER_SECOND)
                  .getPresetValues()
                  .getDefaultValue());
    validateThrottleParams(specParams, currentParamsMap);
    com.yugabyte.yw.forms.YbcThrottleParameters newParams =
        new com.yugabyte.yw.forms.YbcThrottleParameters();
    // We are casting a Long to an int, but this is only because the java code generated from the
    // CRD uses Longs.
    newParams.maxConcurrentDownloads = specParams.getMaxConcurrentDownloads();
    newParams.maxConcurrentUploads = specParams.getMaxConcurrentUploads();
    newParams.perDownloadNumObjects = specParams.getPerDownloadNumObjects();
    newParams.perUploadNumObjects = specParams.getPerUploadNumObjects();
    newParams.diskReadBytesPerSecond = specParams.getDiskReadBytesPerSec();
    newParams.diskReadBytesPerSecond = specParams.getDiskWriteBytesPerSec();
    ybcManager.setThrottleParams(universe.getUniverseUUID(), newParams);
  }

  /** Normalized per-AZ volume data used by shared override logic. */
  private static class PerAZVolumeDto {
    final Integer volumeSize;
    final Integer numVolumes;
    final String storageClass;

    PerAZVolumeDto(Integer volumeSize, Integer numVolumes, String storageClass) {
      this.volumeSize = volumeSize;
      this.numVolumes = numVolumes;
      this.storageClass = storageClass;
    }
  }

  private UserIntentOverrides applyUserIntentOverridesFromPerAZ(
      Provider provider,
      Map<UniverseTaskBase.ServerType, Map<String, PerAZVolumeDto>> perAZByServerType) {
    if (MapUtils.isEmpty(perAZByServerType)) {
      return null;
    }
    Set<AvailabilityZone> zones = new HashSet<>();
    for (com.yugabyte.yw.models.Region region : provider.getRegions()) {
      for (AvailabilityZone az : region.getZones()) {
        zones.add(az);
      }
    }
    Map<UUID, AZOverrides> azOverridesMap = new HashMap<>();

    for (Map.Entry<UniverseTaskBase.ServerType, Map<String, PerAZVolumeDto>> entry :
        perAZByServerType.entrySet()) {
      UniverseTaskBase.ServerType serverType = entry.getKey();
      Map<String, PerAZVolumeDto> perAZMap = entry.getValue();
      if (MapUtils.isEmpty(perAZMap)) {
        continue;
      }
      for (Map.Entry<String, PerAZVolumeDto> azEntry : perAZMap.entrySet()) {
        String azCode = azEntry.getKey();
        PerAZVolumeDto dto = azEntry.getValue();
        Optional<AvailabilityZone> aZoneOpt =
            zones.stream().filter(zone -> zone.getCode().equals(azCode)).findAny();
        if (aZoneOpt.isEmpty()) {
          throw new RuntimeException(
              String.format("Availability zone with code '%s' not found in provider", azCode));
        }
        AvailabilityZone aZone = aZoneOpt.get();

        AZOverrides azOverrides = azOverridesMap.getOrDefault(aZone.getUuid(), new AZOverrides());
        Map<UniverseTaskBase.ServerType, PerProcessDetails> perProcessMap =
            azOverrides.getPerProcess() != null ? azOverrides.getPerProcess() : new HashMap<>();

        DeviceInfo deviceInfo = new DeviceInfo();
        if (dto.volumeSize != null) {
          deviceInfo.volumeSize = dto.volumeSize;
        }
        if (dto.numVolumes != null) {
          deviceInfo.numVolumes = dto.numVolumes;
        }
        if (StringUtils.isNotBlank(dto.storageClass)) {
          deviceInfo.storageClass = dto.storageClass;
        }

        if (!deviceInfo.allNull()) {
          PerProcessDetails details = new PerProcessDetails();
          details.setDeviceInfo(deviceInfo);
          perProcessMap.put(serverType, details);
          azOverrides.setPerProcess(perProcessMap);
          azOverridesMap.put(aZone.getUuid(), azOverrides);
        }
      }
    }

    UserIntentOverrides userIntentOverrides = new UserIntentOverrides();
    if (!azOverridesMap.isEmpty()) {
      userIntentOverrides.setAzOverrides(azOverridesMap);
    }
    return userIntentOverrides;
  }

  private void applyPrimaryClusterUserIntentOverrides(
      Provider provider,
      io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume,
      io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume masterVolume,
      UserIntent userIntent) {
    Map<UniverseTaskBase.ServerType, Map<String, PerAZVolumeDto>> perAZByServerType =
        new HashMap<>();
    if (tserverVolume != null && tserverVolume.getPerAZ() != null) {
      perAZByServerType.put(
          UniverseTaskBase.ServerType.TSERVER,
          toPerAZVolumeDtoMapFromTserver(tserverVolume.getPerAZ()));
    }
    if (masterVolume != null && masterVolume.getPerAZ() != null) {
      perAZByServerType.put(
          UniverseTaskBase.ServerType.MASTER,
          toPerAZVolumeDtoMapFromMaster(masterVolume.getPerAZ()));
    }
    UserIntentOverrides overrides = applyUserIntentOverridesFromPerAZ(provider, perAZByServerType);
    userIntent.setUserIntentOverrides(
        (overrides == null || overrides.allNull()) ? null : overrides);
  }

  private void applyReadReplicaUserIntentOverrides(
      Provider provider,
      io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverVolume tserverVolume,
      UserIntent userIntent) {
    if (tserverVolume == null || tserverVolume.getPerAZ() == null) {
      return;
    }
    Map<UniverseTaskBase.ServerType, Map<String, PerAZVolumeDto>> perAZByServerType =
        new HashMap<>();
    perAZByServerType.put(
        UniverseTaskBase.ServerType.TSERVER,
        toPerAZVolumeDtoMapReadReplica(tserverVolume.getPerAZ()));
    UserIntentOverrides overrides = applyUserIntentOverridesFromPerAZ(provider, perAZByServerType);
    userIntent.setUserIntentOverrides(
        (overrides == null || overrides.allNull()) ? null : overrides);
  }

  /**
   * Recomputes the userIntentOverrides for kubernetes operator-controlled universes after
   * CRUDHandler configure has finalized the placement. Decisions are made independently per-cluster
   * and per-server-type:
   *
   * <p>If the spec defines perAZ for a given server type (tserver / master / read replica tserver),
   * the resulting overrides for that server type come only from the perAZ entries (other sources
   * such as provider storage class, provider/AZ helm overrides and universe-level helm overrides
   * are ignored). Existing per-AZ entries previously stored on the userIntent for that server type
   * are wiped first so the spec is the single source of truth.
   *
   * <p>Otherwise, KubernetesUtil.generateVolumeOverridesForUserIntent is used, which layers in
   * provider storage class, provider/AZ helm overrides, universe-level helm overrides and any
   * AZ-level helm overrides. For edits, AZs that already exist on the saved placement (the
   * "retained" set) keep whatever overrides were stored on the userIntent, only newly added AZs get
   * freshly computed overrides. For creates the saved placement is null, so all AZs are freshly
   * computed.
   *
   * <p>The cases for tserver, master and the read replica tserver are handled independently, having
   * perAZ for one does not change the source selection for the others. An empty perAZ block still
   * counts as "present" and disables the fallback for that server type.
   */
  void applyKubernetesOperatorVolumeOverrides(
      UniverseDefinitionTaskParams taskParams,
      YBUniverse ybUniverse,
      UUID customerUUID,
      @Nullable Universe existingUniverse) {
    Provider provider = getProvider(ybUniverse, customerUUID);

    Cluster primaryCluster = taskParams.getPrimaryCluster();
    // The universe-level helm overrides (universeOverrides string and azOverrides map) are stored
    // on the primary cluster's userIntent. Read-replica's own userIntent does not carry these.
    String primaryUniverseOverrides =
        primaryCluster != null ? primaryCluster.userIntent.universeOverrides : null;
    Map<String, String> primaryAzOverrides =
        primaryCluster != null ? primaryCluster.userIntent.azOverrides : null;
    if (primaryCluster != null
        && (existingUniverse == null
            || taskParams.currentClusterType.equals(ClusterType.PRIMARY))) {
      PlacementInfo savedPlacementInfo =
          (existingUniverse != null
                  && existingUniverse.getUniverseDetails().getPrimaryCluster() != null)
              ? existingUniverse.getUniverseDetails().getPrimaryCluster().placementInfo
              : null;
      applyOverridesForCluster(
          provider,
          primaryCluster,
          ybUniverse.getSpec().getTserverVolume(),
          ybUniverse.getSpec().getMasterVolume(),
          null /* readReplicaTserverVolume */,
          false /* isReadReplica */,
          savedPlacementInfo,
          primaryUniverseOverrides,
          primaryAzOverrides);
    }

    if (CollectionUtils.isNotEmpty(taskParams.getReadOnlyClusters())
        && ybUniverse.getSpec().getReadReplica() != null
        && (existingUniverse == null || taskParams.currentClusterType.equals(ClusterType.ASYNC))) {
      Cluster rrCluster = taskParams.getReadOnlyClusters().get(0);
      PlacementInfo savedPlacementInfo = null;
      if (existingUniverse != null
          && CollectionUtils.isNotEmpty(
              existingUniverse.getUniverseDetails().getReadOnlyClusters())) {
        savedPlacementInfo =
            existingUniverse.getUniverseDetails().getReadOnlyClusters().get(0).placementInfo;
      }
      applyOverridesForCluster(
          provider,
          rrCluster,
          null /* tserverVolume */,
          null /* masterVolume */,
          ybUniverse.getSpec().getReadReplica().getTserverVolume(),
          true /* isReadReplica */,
          savedPlacementInfo,
          primaryUniverseOverrides,
          primaryAzOverrides);
    }
  }

  private void applyOverridesForCluster(
      Provider provider,
      Cluster cluster,
      io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume,
      io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume masterVolume,
      io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverVolume rrTserverVolume,
      boolean isReadReplica,
      @Nullable PlacementInfo savedPlacementInfo,
      @Nullable String universeOverridesStr,
      @Nullable Map<String, String> azOverrides) {
    UserIntent userIntent = cluster.userIntent;
    PlacementInfo placementInfo = cluster.placementInfo;
    if (placementInfo == null) {
      // Nothing we can do without a finalized placement.
      return;
    }
    Set<UUID> azUUIDs = placementInfo.getAllAZUUIDs();

    // Step 0: for the new cluster create case, merge universe-level helm overrides into the base
    // tserver/master deviceInfo on the userIntent before any AZ-level overrides are computed. On
    // edits the base deviceInfo was already established at create time, so we leave it alone to
    // avoid re-applying the same overrides.
    if (savedPlacementInfo == null) {
      KubernetesUtil.applyUniverseOverridesToBaseDeviceInfo(userIntent, universeOverridesStr);
    }

    // Step 1: collect perAZ-derived overrides per server type, and remember which server types
    // have a perAZ block in the spec (so the fallback is skipped for them).
    Map<UniverseTaskBase.ServerType, Map<String, PerAZVolumeDto>> perAZByServerType =
        new HashMap<>();
    Set<UniverseTaskBase.ServerType> serverTypesWithPerAZ =
        EnumSet.noneOf(UniverseTaskBase.ServerType.class);

    if (isReadReplica) {
      if (rrTserverVolume != null && rrTserverVolume.getPerAZ() != null) {
        serverTypesWithPerAZ.add(UniverseTaskBase.ServerType.TSERVER);
        perAZByServerType.put(
            UniverseTaskBase.ServerType.TSERVER,
            toPerAZVolumeDtoMapReadReplica(rrTserverVolume.getPerAZ()));
      }
    } else {
      if (tserverVolume != null && tserverVolume.getPerAZ() != null) {
        serverTypesWithPerAZ.add(UniverseTaskBase.ServerType.TSERVER);
        perAZByServerType.put(
            UniverseTaskBase.ServerType.TSERVER,
            toPerAZVolumeDtoMapFromTserver(tserverVolume.getPerAZ()));
      }
      if (masterVolume != null && masterVolume.getPerAZ() != null) {
        serverTypesWithPerAZ.add(UniverseTaskBase.ServerType.MASTER);
        perAZByServerType.put(
            UniverseTaskBase.ServerType.MASTER,
            toPerAZVolumeDtoMapFromMaster(masterVolume.getPerAZ()));
      }
    }

    // Step 2: start from the existing userIntentOverrides (clone so we don't mutate state we
    // don't intend to). This is what carries over for the fallback case, existing per-AZ
    // entries for retained AZs must be preserved as-is.
    UserIntentOverrides combined =
        userIntent.getUserIntentOverrides() != null
            ? userIntent.getUserIntentOverrides().clone()
            : new UserIntentOverrides();

    // Step 3: for server types that have perAZ in spec, remove any existing entries for those
    // server types so that the spec is the only source of truth (per design: perAZ presence,
    // even if empty, fully controls overrides for that server type).
    if (!serverTypesWithPerAZ.isEmpty()) {
      stripServerTypeEntries(combined, serverTypesWithPerAZ);
    }

    // Step 4: add perAZ-derived entries for the server types that have perAZ in spec.
    UserIntentOverrides perAZBuilt = applyUserIntentOverridesFromPerAZ(provider, perAZByServerType);
    if (perAZBuilt != null && perAZBuilt.getAzOverrides() != null) {
      mergeAzOverrides(combined, perAZBuilt);
    }

    // Step 5: for the server types that did not specify perAZ, run the standard fallback
    // (generateVolumeOverridesForUserIntent) restricted to those server types. Pass the retained
    // AZs as skipAZs on edits so existing AZs keep their saved overrides while only newly added
    // AZs are recomputed. On create (savedPlacementInfo == null), skipAZs is null so all AZs are
    // freshly computed.
    Set<UniverseTaskBase.ServerType> serverTypesForFallback =
        isReadReplica
            ? EnumSet.of(UniverseTaskBase.ServerType.TSERVER)
            : EnumSet.of(UniverseTaskBase.ServerType.TSERVER, UniverseTaskBase.ServerType.MASTER);
    serverTypesForFallback.removeAll(serverTypesWithPerAZ);
    if (!serverTypesForFallback.isEmpty()) {
      Set<UUID> skipAZs =
          savedPlacementInfo == null
              ? null
              : PlacementInfoUtil.findRetainedAZs(placementInfo, savedPlacementInfo);
      UserIntentOverrides fallback =
          KubernetesUtil.generateVolumeOverridesForUserIntent(
              combined,
              azUUIDs,
              universeOverridesStr,
              azOverrides,
              skipAZs,
              serverTypesForFallback);
      if (fallback != null) {
        combined = fallback;
      }
    }

    userIntent.setUserIntentOverrides((combined == null || combined.allNull()) ? null : combined);
    if (userIntent.getUserIntentOverrides() != null) {
      userIntent.getUserIntentOverrides().removeNonRequiredAZs(azUUIDs);
    }
  }

  /**
   * Removes per-process entries for the given serverTypes from every AZ override in overrides. AZ
   * entries that become empty are dropped; if overrides ends up with no remaining AZ data, its
   * azOverrides map is cleared.
   */
  private static void stripServerTypeEntries(
      UserIntentOverrides overrides, Set<UniverseTaskBase.ServerType> serverTypes) {
    if (overrides == null
        || MapUtils.isEmpty(overrides.getAzOverrides())
        || serverTypes.isEmpty()) {
      return;
    }
    Iterator<Map.Entry<UUID, AZOverrides>> iter = overrides.getAzOverrides().entrySet().iterator();
    while (iter.hasNext()) {
      Map.Entry<UUID, AZOverrides> entry = iter.next();
      AZOverrides azOverrides = entry.getValue();
      if (azOverrides == null) {
        iter.remove();
        continue;
      }
      Map<UniverseTaskBase.ServerType, PerProcessDetails> perProcess = azOverrides.getPerProcess();
      if (MapUtils.isNotEmpty(perProcess)) {
        perProcess.keySet().removeAll(serverTypes);
        if (MapUtils.isEmpty(perProcess)) {
          azOverrides.setPerProcess(null);
        }
      }
      if (azOverrides.allNull()) {
        iter.remove();
      }
    }
    if (MapUtils.isEmpty(overrides.getAzOverrides())) {
      overrides.setAzOverrides(null);
    }
  }

  /**
   * Merges source's per-AZ overrides into target. For overlapping AZs the source's per-process
   * entries win on a per-server-type basis, leaving any other per-process entries on the target
   * intact. The deviceInfo on overlapping AZs is replaced by the source's when present.
   */
  private static void mergeAzOverrides(UserIntentOverrides target, UserIntentOverrides source) {
    if (source == null || MapUtils.isEmpty(source.getAzOverrides())) {
      return;
    }
    Map<UUID, AZOverrides> targetMap =
        target.getAzOverrides() != null ? target.getAzOverrides() : new HashMap<>();
    for (Map.Entry<UUID, AZOverrides> entry : source.getAzOverrides().entrySet()) {
      AZOverrides sourceAz = entry.getValue();
      if (sourceAz == null) {
        continue;
      }
      AZOverrides targetAz = targetMap.computeIfAbsent(entry.getKey(), k -> new AZOverrides());
      if (MapUtils.isNotEmpty(sourceAz.getPerProcess())) {
        Map<UniverseTaskBase.ServerType, PerProcessDetails> targetPerProcess =
            targetAz.getPerProcess() != null ? targetAz.getPerProcess() : new HashMap<>();
        targetPerProcess.putAll(sourceAz.getPerProcess());
        targetAz.setPerProcess(targetPerProcess);
      }
    }
    target.setAzOverrides(targetMap);
  }

  private static Map<String, PerAZVolumeDto> toPerAZVolumeDtoMapFromTserver(
      Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ> perAZ) {
    Map<String, PerAZVolumeDto> map = new HashMap<>();
    if (MapUtils.isEmpty(perAZ)) {
      return map;
    }
    for (Map.Entry<String, io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ> e :
        perAZ.entrySet()) {
      map.put(e.getKey(), toPerAZVolumeDtoFromTserver(e.getValue()));
    }
    return map;
  }

  private static PerAZVolumeDto toPerAZVolumeDtoFromTserver(
      io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az) {
    if (az == null) {
      return new PerAZVolumeDto(null, null, null);
    }
    return new PerAZVolumeDto(
        az.getVolumeSize() != null ? az.getVolumeSize().intValue() : null,
        az.getNumVolumes() != null ? az.getNumVolumes().intValue() : null,
        az.getStorageClass());
  }

  private static Map<String, PerAZVolumeDto> toPerAZVolumeDtoMapFromMaster(
      Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ> perAZ) {
    Map<String, PerAZVolumeDto> map = new HashMap<>();
    if (MapUtils.isEmpty(perAZ)) {
      return map;
    }
    for (Map.Entry<String, io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ> e :
        perAZ.entrySet()) {
      map.put(e.getKey(), toPerAZVolumeDtoFromMaster(e.getValue()));
    }
    return map;
  }

  private static PerAZVolumeDto toPerAZVolumeDtoFromMaster(
      io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ az) {
    if (az == null) {
      return new PerAZVolumeDto(null, null, null);
    }
    return new PerAZVolumeDto(
        az.getVolumeSize() != null ? az.getVolumeSize().intValue() : null,
        az.getNumVolumes() != null ? az.getNumVolumes().intValue() : null,
        az.getStorageClass());
  }

  private static Map<String, PerAZVolumeDto> toPerAZVolumeDtoMapReadReplica(
      Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.tservervolume.PerAZ>
          perAZ) {
    Map<String, PerAZVolumeDto> map = new HashMap<>();
    if (MapUtils.isEmpty(perAZ)) {
      return map;
    }
    for (Map.Entry<
            String, io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.tservervolume.PerAZ>
        e : perAZ.entrySet()) {
      map.put(e.getKey(), toPerAZVolumeDtoFromReadReplica(e.getValue()));
    }
    return map;
  }

  private static PerAZVolumeDto toPerAZVolumeDtoFromReadReplica(
      io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.tservervolume.PerAZ az) {
    if (az == null) {
      return new PerAZVolumeDto(null, null, null);
    }
    return new PerAZVolumeDto(
        az.getVolumeSize() != null ? az.getVolumeSize().intValue() : null,
        az.getNumVolumes() != null ? az.getNumVolumes().intValue() : null,
        az.getStorageClass());
  }

  /**
   * Validate the throttle parameters.
   *
   * <p>This method will validate the throttle parameters with the preset values from YBC. If any of
   * the throttle parameters are out of the preset range, an error message will be added to the
   * errors list.
   *
   * @param specParams the throttle parameters to validate
   * @param currentParamsMap the map of current throttle parameters and their preset values
   * @return a list of error messages
   */
  private void validateThrottleParams(
      YbcThrottleParameters specParams, Map<String, ThrottleParamValue> currentParamsMap) {
    List<String> errors = new ArrayList<>();
    if (specParams.getMaxConcurrentDownloads() != null
        && specParams.getMaxConcurrentDownloads()
            > currentParamsMap
                .get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS)
                .getPresetValues()
                .getMaxValue()) {
      errors.add(
          "Max concurrent downloads cannot be greater than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS)
                  .getPresetValues()
                  .getMaxValue());
    } else if (specParams.getMaxConcurrentDownloads() != null
        && specParams.getMaxConcurrentDownloads()
            < currentParamsMap
                .get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS)
                .getPresetValues()
                .getMinValue()) {
      errors.add(
          "Max concurrent downloads cannot be less than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS)
                  .getPresetValues()
                  .getMinValue());
    }
    if (specParams.getMaxConcurrentUploads() != null
        && specParams.getMaxConcurrentUploads()
            > currentParamsMap
                .get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS)
                .getPresetValues()
                .getMaxValue()) {
      errors.add(
          "Max concurrent uploads cannot be greater than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS)
                  .getPresetValues()
                  .getMaxValue());
    } else if (specParams.getMaxConcurrentUploads() != null
        && specParams.getMaxConcurrentUploads()
            < currentParamsMap
                .get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS)
                .getPresetValues()
                .getMinValue()) {
      errors.add(
          "Max concurrent uploads cannot be less than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS)
                  .getPresetValues()
                  .getMinValue());
    }
    if (specParams.getPerDownloadNumObjects() != null
        && specParams.getPerDownloadNumObjects()
            > currentParamsMap
                .get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS)
                .getPresetValues()
                .getMaxValue()) {
      errors.add(
          "Per download objects cannot be greater than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS)
                  .getPresetValues()
                  .getMaxValue());
    } else if (specParams.getPerDownloadNumObjects() != null
        && specParams.getPerDownloadNumObjects()
            < currentParamsMap
                .get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS)
                .getPresetValues()
                .getMinValue()) {
      errors.add(
          "Per download objects cannot be less than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS)
                  .getPresetValues()
                  .getMinValue());
    }
    if (specParams.getPerUploadNumObjects() != null
        && specParams.getPerUploadNumObjects()
            > currentParamsMap
                .get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS)
                .getPresetValues()
                .getMaxValue()) {
      errors.add(
          "Per upload objects cannot be greater than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS)
                  .getPresetValues()
                  .getMaxValue());
    } else if (specParams.getPerUploadNumObjects() != null
        && specParams.getPerUploadNumObjects()
            < currentParamsMap
                .get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS)
                .getPresetValues()
                .getMinValue()) {
      errors.add(
          "Per upload objects cannot be less than "
              + currentParamsMap
                  .get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS)
                  .getPresetValues()
                  .getMinValue());
    }
    if (!errors.isEmpty()) {
      log.error("found errors: {}", errors);
      throw new IllegalArgumentException(String.join("\n", errors));
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

  private void createAutoProviderCR(YBUniverse ybUniverse, String providerName, UUID customerUUID) {
    List<String> zonesFilter = ybUniverse.getSpec().getZoneFilter();
    String storageClass =
        ybUniverse.getSpec().getDeviceInfo() != null
            ? ybUniverse.getSpec().getDeviceInfo().getStorageClass()
            : null;
    String kubeNamespace = ybUniverse.getMetadata().getNamespace();
    String domainName =
        maybeGetKubeDomainFromOverrides(ybUniverse.getSpec().getKubernetesOverrides());
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
                                if (StringUtils.isNotBlank(storageClass)) {
                                  tempMap.put("STORAGE_CLASS", storageClass);
                                }
                                if (StringUtils.isNotBlank(domainName)) {
                                  tempMap.put("KUBE_DOMAIN", domainName);
                                }
                                tempMap.put("KUBENAMESPACE", kubeNamespace);
                                z.config = tempMap;
                                return z;
                              })
                          .collect(Collectors.toList());
                  return r;
                })
            .collect(Collectors.toList());
    providerData.name = providerName;

    operatorUtils.createProviderCrFromProviderEbean(providerData, kubeNamespace, true);
  }

  /**
   * Gets the provider for the universe. Returns the provider if found, or null if not found. This
   * method only retrieves existing providers, it does not create new ones.
   */
  private Provider getProvider(YBUniverse ybUniverse, UUID customerUUID) {
    String providerName = ybUniverse.getSpec().getProviderName();

    if (StringUtils.isNotBlank(providerName)) {
      // Case when provider name is available in spec: Use that, or return null.
      Provider provider = Provider.get(customerUUID, providerName, CloudType.kubernetes);
      if (provider != null) {
        log.info("Using provider from custom resource spec.");
        return provider;
      } else {
        log.error("Provider {} not found", providerName);
        log.error(
            "Please create a provider with name {}. Skipping universe creation.", providerName);
        kubernetesStatusUpdater.updateUniverseState(
            KubernetesResourceDetails.fromResource(ybUniverse), UniverseState.ERROR_CREATING);
        throw new RuntimeException("Provider " + providerName + " not found");
      }
    } else {
      // Case when provider name is not available in spec
      providerName = getProviderName(getUniverseName(ybUniverse));
      Provider provider = Provider.get(customerUUID, providerName, CloudType.kubernetes);
      if (provider != null) {
        // If auto-provider with the same name found return it.
        log.info("Found auto-provider with name {}", providerName);
        // Clean up the tracking set since provider is now ready
        inProgressAutoProviderCRs.remove(providerName);
        return provider;
      } else {
        log.debug("Auto-provider {} not found", providerName);
        return null;
      }
    }
  }

  /**
   * Creates auto-provider if needed and not already created. This method handles the logic for when
   * auto-provider creation is applicable.
   */
  private void createAutoProvider(YBUniverse ybUniverse, UUID customerUUID) {
    // Only create auto-provider if running in Kubernetes and no provider name specified
    if (StringUtils.isNotBlank(ybUniverse.getSpec().getProviderName())
        || !KubernetesEnvironmentVariables.isYbaRunningInKubernetes()) {
      return;
    }
    String providerName = getProviderName(getUniverseName(ybUniverse));
    // Check if we've already initiated creation of this provider CR
    if (inProgressAutoProviderCRs.contains(providerName)) {
      log.info("Auto-provider {} creation already initiated, skipping", providerName);
      return;
    }
    // Create auto-provider for Kubernetes based installation
    log.info("Creating auto-provider with name {}", providerName);
    try {
      createAutoProviderCR(ybUniverse, providerName, customerUUID);
      inProgressAutoProviderCRs.add(providerName);
    } catch (Exception e) {
      log.error("Unable to create auto-provider: {}", e.getMessage());
      throw new RuntimeException("Unable to create auto-provider", e);
    }
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
          "universe {} is locked, requeue update and try again later", getUniverseName(ybUniverse));
      workqueue.requeue(OperatorWorkQueue.getWorkQueueKey(ybUniverse.getMetadata()), action, false);
      log.debug("scheduled universe update for requeue");
      return true;
    }
    return false;
  }

  private Release maybeGetReleaseCr(String releaseVersion) {
    Lister<Release> releaseLister = new Lister<>(this.releaseInformer.getIndexer());
    List<Release> releases = releaseLister.list();
    for (Release release : releases) {
      if (release.getSpec().getConfig().getVersion().equals(releaseVersion)) {
        return release;
      }
    }
    log.info("Unable to find release cr for version {}", releaseVersion);
    return null;
  }

  private void maybeDeleteRelease(YBUniverse ybUniverse) {
    try {
      String releaseVersion = ybUniverse.getSpec().getYbSoftwareVersion();
      Release release = maybeGetReleaseCr(releaseVersion);
      if (release != null && release.getMetadata().getDeletionTimestamp() != null) {
        log.info("Cleaning up release - {}", release.getMetadata().getName());
        operatorUtils.deleteReleaseCr(release);
      }
    } catch (Exception e) {
      log.error("Error deleting release cr", e);
    }
  }

  private String maybeGetKubeDomainFromOverrides(KubernetesOverrides overrides) {
    if (overrides != null && overrides.getAdditionalProperties() != null) {
      for (Map.Entry<String, Object> entry : overrides.getAdditionalProperties().entrySet()) {
        if (entry.getKey().equals("domainName")) {
          return entry.getValue().toString();
        }
      }
    }
    return null;
  }

  public static String getUniverseName(YBUniverse ybUniverse) {
    if (ybUniverse.getSpec().getUniverseName() != null) {
      return ybUniverse.getSpec().getUniverseName();
    }
    return OperatorUtils.getYbaResourceName(ybUniverse.getMetadata());
  }

  /**
   * Adds a read replica cluster to the universe details based on the YBUniverse specification.
   *
   * @param universeDetails the universe details to modify
   * @param ybUniverse the YBUniverse specification
   * @param customerUUID the customer UUID for placement info creation
   */
  private void addReadReplicaClusterToUniverseDetails(
      UniverseDefinitionTaskParams universeDetails, YBUniverse ybUniverse, UUID customerUUID) {
    UserIntent readReplicaUserIntent =
        getReadReplicaUserIntent(
            universeDetails.getPrimaryCluster().userIntent, ybUniverse, customerUUID);
    Cluster readReplicaCluster = new Cluster(ClusterType.ASYNC, readReplicaUserIntent);
    if (ybUniverse.getSpec().getReadReplica().getPlacementInfo() != null) {
      readReplicaCluster.placementInfo =
          createPlacementInfo(ybUniverse, customerUUID, /*isReadOnlyCluster*/ true, null);
      universeDetails.userAZSelected = true;
    }
    universeDetails.clusters.add(readReplicaCluster);
  }

  private UserIntent getReadReplicaUserIntent(
      UserIntent userIntent, YBUniverse ybUniverse, UUID customerUUID) {
    UserIntent readReplicaUserIntent = userIntent.clone();
    readReplicaUserIntent.numNodes = ybUniverse.getSpec().getReadReplica().getNumNodes().intValue();
    readReplicaUserIntent.replicationFactor =
        ybUniverse.getSpec().getReadReplica().getReplicationFactor().intValue();

    // Use new tserverVolume field if present, otherwise fall back to old deviceInfo field
    if (ybUniverse.getSpec().getReadReplica().getTserverVolume() != null) {
      readReplicaUserIntent.deviceInfo =
          operatorUtils.mapReadReplicaTserverVolume(
              ybUniverse.getSpec().getReadReplica().getTserverVolume());
    } else if (ybUniverse.getSpec().getReadReplica().getDeviceInfo() != null) {
      readReplicaUserIntent.deviceInfo.volumeSize =
          ybUniverse.getSpec().getReadReplica().getDeviceInfo().getVolumeSize().intValue();
      readReplicaUserIntent.deviceInfo.numVolumes =
          ybUniverse.getSpec().getReadReplica().getDeviceInfo().getNumVolumes().intValue();
    }
    if (ybUniverse.getSpec().getReadReplica().getTserverVolume() != null
        && ybUniverse.getSpec().getReadReplica().getTserverVolume().getPerAZ() != null) {
      applyReadReplicaUserIntentOverrides(
          getProvider(ybUniverse, customerUUID),
          ybUniverse.getSpec().getReadReplica().getTserverVolume(),
          readReplicaUserIntent);
    }
    readReplicaUserIntent.tserverK8SNodeResourceSpec =
        operatorUtils.toNodeResourceSpec(
            ybUniverse.getSpec().getReadReplica().getTserverResourceSpec(),
            s -> s.getCpu(),
            s -> s.getMemory());
    return readReplicaUserIntent;
  }

  /**
   * Updates the existing read replica cluster in the universe details based on the YBUniverse
   * specification.
   *
   * @param universeDetails the universe details to modify
   * @param ybUniverse the YBUniverse specification
   * @param customerUUID the customer UUID for placement info creation
   */
  private void updateReadReplicaClusterInUniverseDetails(
      UniverseDefinitionTaskParams universeDetails,
      YBUniverse ybUniverse,
      UUID customerUUID,
      UserIntent incomingReadReplicaIntent) {
    Cluster existingReadReplicaCluster = universeDetails.getReadOnlyClusters().get(0);
    UserIntent existingUserIntent = existingReadReplicaCluster.userIntent;
    existingUserIntent.numNodes = incomingReadReplicaIntent.numNodes;
    existingUserIntent.replicationFactor = incomingReadReplicaIntent.replicationFactor;
    if (ybUniverse.getSpec().getReadReplica().getTserverVolume() != null) {
      existingUserIntent.deviceInfo = incomingReadReplicaIntent.deviceInfo;
    } else {
      existingUserIntent.deviceInfo.volumeSize = incomingReadReplicaIntent.deviceInfo.volumeSize;
    }
    if (ybUniverse.getSpec().getReadReplica().getTserverVolume() != null
        && ybUniverse.getSpec().getReadReplica().getTserverVolume().getPerAZ() != null) {
      existingUserIntent.updateAZVolumeOverrides(
          incomingReadReplicaIntent,
          existingReadReplicaCluster.placementInfo.getAllAZUUIDs(),
          null,
          false);
    }
    existingUserIntent.tserverK8SNodeResourceSpec =
        incomingReadReplicaIntent.tserverK8SNodeResourceSpec;
    if (ybUniverse.getSpec().getReadReplica().getPlacementInfo() != null) {
      existingReadReplicaCluster.placementInfo =
          createPlacementInfo(
              ybUniverse,
              customerUUID,
              true /*isReadOnlyCluster*/,
              existingReadReplicaCluster.placementInfo);
      universeDetails.userAZSelected = true;
    }
  }

  private UUID addReadReplicaCluster(
      Customer cust, UniverseDefinitionTaskParams universeDetails, YBUniverse ybUniverse) {
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
              mapper.writeValueAsString(universeDetails), UniverseConfigureTaskParams.class);
    } catch (Exception e) {
      log.error("Failed at creating configure task params for edit", e);
    }
    taskConfigParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    taskConfigParams.currentClusterType = ClusterType.ASYNC;
    taskConfigParams.isKubernetesOperatorControlled = true;
    taskConfigParams.setKubernetesResourceDetails(
        KubernetesResourceDetails.fromResource(ybUniverse));
    log.info("Adding read replica cluster to universe now");
    universeCRUDHandler.configure(cust, taskConfigParams);
    // CRUDHandler skips userIntentOverrides handling for operator-controlled universes; recompute
    // them here so the read replica cluster's overrides are populated correctly.
    Universe existingUniverse = Universe.maybeGet(universeDetails.getUniverseUUID()).orElse(null);
    applyKubernetesOperatorVolumeOverrides(
        taskConfigParams, ybUniverse, cust.getUuid(), existingUniverse);
    taskConfigParams.clusters.remove(
        0); // Remove primary cluster since the createCluster accepts only RR
    Universe universe = Universe.getOrBadRequest(universeDetails.getUniverseUUID());
    return universeCRUDHandler.createCluster(cust, universe, taskConfigParams);
  }
}
