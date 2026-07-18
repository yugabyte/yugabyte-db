// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Customer;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.CustomResource;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import java.util.Objects;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public abstract class AbstractReconciler<T extends CustomResource<?, ?>>
    implements ResourceEventHandler<T>, Runnable {
  protected final KubernetesClient client;
  protected final YBInformerFactory informerFactory;
  protected final Lister<T> lister;
  protected final MixedOperation<T, KubernetesResourceList<T>, Resource<T>> resourceClient;
  protected final SharedIndexInformer<T> resourceInformer;
  protected final OperatorUtils operatorUtils;
  protected final OperatorWorkQueue workqueue;
  protected final String namespace;
  private final Class<T> clazz;
  protected final Integer reconcileExceptionBackoffMS = 5000;

  protected final ResourceTracker resourceTracker = new ResourceTracker();

  // The resource currently being reconciled. Set before calling create/update/noOp handlers
  // so that child classes can associate dependent resources (e.g. secrets) with the correct owner.
  protected KubernetesResourceDetails currentReconcileResource;

  public AbstractReconciler(
      KubernetesClient client,
      YBInformerFactory informerFactory,
      Class<T> clazz,
      OperatorUtils operatorUtils,
      String namespace) {
    this.client = client;
    this.clazz = clazz;
    this.resourceClient = client.resources(clazz);
    this.informerFactory = informerFactory;
    this.resourceInformer = informerFactory.getSharedIndexInformer(clazz, client);
    this.lister = new Lister<>(this.resourceInformer.getIndexer());
    this.operatorUtils = operatorUtils;
    this.workqueue = new OperatorWorkQueue(clazz.getSimpleName());
    this.namespace = namespace;
    this.resourceInformer.addEventHandler(this);
  }

  // Abstract methods
  protected abstract void createActionReconcile(T resource, Customer cust) throws Exception;

  protected abstract void updateActionReconcile(T resource, Customer cust) throws Exception;

  protected abstract void noOpActionReconcile(T resource, Customer cust) throws Exception;

  protected abstract void handleResourceDeletion(
      T resource, Customer cust, OperatorWorkQueue.ResourceAction action) throws Exception;

  public Set<KubernetesResourceDetails> getTrackedResources() {
    return resourceTracker.getTrackedResources();
  }

  public ResourceTracker getResourceTracker() {
    return resourceTracker;
  }

  // Implemented methods
  @Override
  public void run() {
    log.info("Starting {} reconciler thread", clazz.getSimpleName());
    while (!Thread.currentThread().isInterrupted()) {
      if (resourceInformer.hasSynced()) {
        break;
      }
    }
    while (true) {
      try {
        log.info("Trying to fetch {} actions from workqueue...", clazz.getSimpleName());
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

        // Get the resource's name
        // from workqueue key which is in format namespace/name/uid.
        // First get key namespace/name format
        String listerKey = OperatorWorkQueue.getListerKeyFromWorkQueueKey(key);
        T resource = lister.get(listerKey);
        log.info("Processing {} name: {}, Object: {}", clazz.getSimpleName(), listerKey, resource);

        if (resource == null) {
          if (action == OperatorWorkQueue.ResourceAction.DELETE) {
            log.info(
                "Tried to delete {} {} but it's no longer in Lister", clazz.getSimpleName(), key);
          }
          // Clear any state of the non-existing resource from In-memory maps
          workqueue.clearState(key);
          continue;
        }
        if (resource
            .getMetadata()
            .getUid()
            .equals(OperatorWorkQueue.getResourceUidFromWorkQueueKey(key))) {
          reconcile(resource, action);
        } else {
          workqueue.clearState(key);
          log.debug(
              "Lister referencing older {} with same name {}, ignoring",
              clazz.getSimpleName(),
              listerKey);
          continue;
        }

      } catch (Exception e) {
        log.error("Got Exception", e);
        try {
          Thread.sleep(reconcileExceptionBackoffMS);
          log.info("Continue {} reconcile loop after failure backoff", clazz.getSimpleName());
        } catch (InterruptedException e1) {
          log.info("caught interrupt, stopping reconciler", e);
          break;
        }
      }
    }
  }

  /**
   * Tries to achieve the desired state for resource.
   *
   * @param resource specified resource
   */
  protected void reconcile(T resource, OperatorWorkQueue.ResourceAction action) {
    String resourceName = resource.getMetadata().getName();
    String resourceNamespace = resource.getMetadata().getNamespace();
    log.info(
        "Reconcile for metadata: Name = {}, Namespace = {}",
        clazz.getSimpleName(),
        resourceName,
        resourceNamespace);

    try {
      Customer cust = operatorUtils.getOperatorCustomer();
      KubernetesResourceDetails resourceDetails = KubernetesResourceDetails.fromResource(resource);
      currentReconcileResource = resourceDetails;

      // checking to see if the resource was deleted.
      if (action == OperatorWorkQueue.ResourceAction.DELETE
          || resource.getMetadata().getDeletionTimestamp() != null) {
        handleResourceDeletion(resource, cust, action);
        Set<KubernetesResourceDetails> orphaned = resourceTracker.untrackResource(resourceDetails);
        log.info("Untracked resource {} and orphaned dependencies: {}", resourceDetails, orphaned);
      } else {
        // Track/update the resource for all non-delete actions so that the persisted
        // YAML in OperatorResource stays current after updates and restarts.
        resourceTracker.trackResource(resource);
        log.trace("Tracking resource {}, all tracked: {}", resourceDetails, getTrackedResources());
        if (action == OperatorWorkQueue.ResourceAction.CREATE) {
          createActionReconcile(resource, cust);
        } else if (action == OperatorWorkQueue.ResourceAction.UPDATE) {
          updateActionReconcile(resource, cust);
        } else if (action == OperatorWorkQueue.ResourceAction.NO_OP) {
          noOpActionReconcile(resource, cust);
        }
      }
    } catch (Exception e) {
      log.error("Got Exception in Operator Action", e);
    }
  }

  // Handle add, update, and delete from the informer
  @Override
  public void onAdd(T resource) {
    enqueue(resource, OperatorWorkQueue.ResourceAction.CREATE);
  }

  @Override
  public void onUpdate(T oldResource, T newResource) {
    if (newResource.getMetadata().getDeletionTimestamp() != null) {
      enqueue(newResource, OperatorWorkQueue.ResourceAction.DELETE);
      return;
    }
    // Treat this as no-op action enqueue
    enqueue(newResource, OperatorWorkQueue.ResourceAction.NO_OP);
  }

  @Override
  public void onDelete(T resource, boolean b) {
    enqueue(resource, OperatorWorkQueue.ResourceAction.DELETE);
  }

  private void enqueue(T resource, OperatorWorkQueue.ResourceAction action) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(resource.getMetadata());
    String listerName = OperatorWorkQueue.getListerKeyFromWorkQueueKey(mapKey);
    log.debug("Enqueue {} {} with action {}", clazz.getSimpleName(), listerName, action.toString());
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
}
