// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.common.operator.utils.TelemetryProviderCrConverter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.yugabyte.operator.v1alpha1.TelemetryProvider;
import io.yugabyte.operator.v1alpha1.TelemetryProviderStatus;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

/**
 * Reconciler for the TelemetryProvider custom resource. Creates and deletes the export destinations
 * (Datadog, Splunk, AWS CloudWatch, GCP Cloud Monitoring, Loki, Dynatrace, S3, OTLP) that DB audit
 * logs, query logs and metrics export send to.
 *
 * <p>The resource is create/delete only. Every spec field is immutable in the CRD and YBA exposes
 * no edit API for a telemetry provider, so a configuration change can never reach an existing
 * provider; changing a provider means deleting the resource and creating a new one, which YBA only
 * permits while no universe references it. The create and no-op passes are therefore both just
 * "make sure the provider exists and the CR reports it".
 *
 * <p>Provider creation is synchronous rather than task based, so {@code status.taskUUID} is never
 * set, and neither is the {@code Creating} state: a reconcile pass either has the provider or
 * reports why it does not. The CR status carries {@code state} ({@code Ready}, {@code Error},
 * {@code InUse}, named as in {@code KMSConfigReconciler}), {@code resourceUUID} and {@code
 * message}. Every pass reconciles the status to the observed state, so a provider that already
 * exists - adopted after being created out of band, or a CR whose earlier status write was lost -
 * is reported {@code Ready} with its resolved UUID rather than left with an empty status.
 *
 * <p>The reconciler stamps {@link OperatorUtils#YB_FINALIZER} on the resource, so a {@code kubectl
 * delete} leaves the object in place with its {@code deletionTimestamp} set until the provider has
 * actually been removed from YBA. That is what makes the delete path observable and retryable: YBA
 * refuses to delete a provider a universe still exports to, and the refusal is reported as {@code
 * InUse} on a live object with the finalizer kept, while a DELETE is requeued so the deletion is
 * retried once the universe stops exporting. The finalizer is removed only once the provider is
 * gone from YBA.
 */
@Slf4j
public class TelemetryProviderReconciler extends AbstractReconciler<TelemetryProvider> {

  // Status states, matching KMSConfigReconciler.
  private static final String STATE_READY = "Ready";
  private static final String STATE_ERROR = "Error";
  private static final String STATE_IN_USE = "InUse";

  private final TelemetryProviderService telemetryProviderService;
  private final TelemetryProviderCrConverter crConverter;

  public TelemetryProviderReconciler(
      TelemetryProviderService telemetryProviderService,
      TelemetryProviderCrConverter crConverter,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory) {
    super(client, informerFactory, TelemetryProvider.class, operatorUtils, namespace);
    this.telemetryProviderService = telemetryProviderService;
    this.crConverter = crConverter;
  }

  @Override
  protected void createActionReconcile(TelemetryProvider provider, Customer cust) {
    log.info("Adding TelemetryProvider: {}", provider.getMetadata().getName());
    processCreation(provider, cust);
  }

  /**
   * The spec is immutable and YBA has no telemetry provider edit API, so there is nothing to push
   * to an existing provider. An UPDATE is only ever reached through a requeue, and is handled like
   * a create: it reconciles the CR to the provider, and creates it if it is still missing.
   */
  @Override
  protected void updateActionReconcile(TelemetryProvider provider, Customer cust) {
    processCreation(provider, cust);
  }

  /**
   * Reconciles the CR status to the provider observed in YBA, and requeues a CREATE if the provider
   * is missing - a create that failed, or a provider deleted out of band.
   */
  @Override
  protected void noOpActionReconcile(TelemetryProvider provider, Customer cust) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(provider.getMetadata());
    com.yugabyte.yw.models.TelemetryProvider ybaProvider;
    try {
      ybaProvider = findExistingProvider(provider, cust);
    } catch (Exception e) {
      log.warn(
          "Failed to look up TelemetryProvider {}: {}",
          provider.getMetadata().getName(),
          e.getMessage());
      return;
    }
    if (ybaProvider == null) {
      log.debug(
          "NoOp Action: TelemetryProvider {} not found in YBA, requeuing Create",
          provider.getMetadata().getName());
      workqueue.requeue(mapKey, ResourceAction.CREATE, false /* incrementRetry */);
      return;
    }
    OperatorUtils.maybeAddYbaResourceId(provider, ybaProvider.getUuid(), resourceClient);
    updateStatus(
        provider, STATE_READY, ybaProvider.getUuid().toString(), "Telemetry provider ready");
    workqueue.resetRetries(mapKey);
  }

  /**
   * Removes the provider from YBA, then releases the object. The finalizer is kept until the
   * provider is confirmed gone: an in-use refusal or a failed delete requeues the DELETE, so the CR
   * stays visible with its status and the deletion is retried.
   */
  @Override
  protected void handleResourceDeletion(
      TelemetryProvider provider, Customer cust, ResourceAction action) {
    log.info("Deleting TelemetryProvider: {}", provider.getMetadata().getName());
    String mapKey = OperatorWorkQueue.getWorkQueueKey(provider.getMetadata());
    boolean providerRemoved;
    try {
      providerRemoved = processDeletion(provider, cust);
    } catch (Exception e) {
      log.error(
          "Failed to delete TelemetryProvider {}: {}",
          provider.getMetadata().getName(),
          e.getMessage(),
          e);
      updateStatus(
          provider, STATE_ERROR, null, "Failed to delete telemetry provider: " + e.getMessage());
      workqueue.requeue(mapKey, ResourceAction.DELETE, true /* incrementRetry */);
      return;
    }
    if (!providerRemoved) {
      // The provider is still in YBA (in use, or the delete failed). processDeletion has recorded
      // the reason on the CR status.
      workqueue.requeue(mapKey, ResourceAction.DELETE, false /* incrementRetry */);
      return;
    }

    try {
      operatorUtils.removeFinalizer(provider, resourceClient);
      workqueue.clearState(mapKey);
    } catch (Exception e) {
      // The object may simply be gone already (a delete event with an unknown final state replays
      // an object whose finalizer was removed earlier), so retry rather than fail the reconcile.
      log.warn(
          "Failed to remove finalizer from TelemetryProvider {}: {}",
          provider.getMetadata().getName(),
          e.getMessage());
      workqueue.requeue(mapKey, ResourceAction.DELETE, true /* incrementRetry */);
    }
  }

  /**
   * Update the status of a TelemetryProvider resource.
   *
   * @param provider the TelemetryProvider resource to update
   * @param state one of Ready, Error, InUse
   * @param resourceUUID the YBA telemetry provider UUID, null leaves any existing value in place
   * @param message the status message
   */
  private void updateStatus(
      TelemetryProvider provider, String state, String resourceUUID, String message) {
    TelemetryProviderStatus status = provider.getStatus();
    if (status == null) {
      status = new TelemetryProviderStatus();
    }
    status.setState(state);
    status.setMessage(message);
    if (resourceUUID != null) {
      // Never clear a UUID that was resolved earlier: a transient error must not make the CR look
      // like the provider was never created.
      status.setResourceUUID(resourceUUID);
    }
    // Telemetry provider creation and deletion are synchronous, there is no task to report.
    status.setTaskUUID(null);
    provider.setStatus(status);
    try {
      resourceClient.inNamespace(namespace).resource(provider).replaceStatus();
    } catch (Exception e) {
      // Best effort: the object may already be gone (status written while handling a delete), and
      // failing to record the status must not abort the reconcile.
      log.warn(
          "Failed to update status of TelemetryProvider {} to {}: {}",
          provider.getMetadata().getName(),
          state,
          e.getMessage());
    }
  }

  /**
   * Creates the telemetry provider in YBA if it does not exist yet. Never throws: a failure is
   * reported on the CR status as an Error and a CREATE is requeued, because there is nothing above
   * the reconcile loop that could act on an exception.
   */
  private void processCreation(TelemetryProvider provider, Customer cust) {
    try {
      // Resolved before the finalizer is stamped: a resource with no usable YBA name has nothing to
      // delete later, so it must not be made undeletable.
      String providerName = getYbaProviderName(provider);
      maybeAddFinalizer(provider);

      com.yugabyte.yw.models.TelemetryProvider existing = findExistingProvider(provider, cust);
      if (existing != null) {
        // Reconcile the CR to the observed state rather than just skipping: an adopted provider
        // (one created through the REST API or the CLI, or a CR whose status write was lost) must
        // still report Ready with its resolved UUID, or a wait-for-Ready gate never completes and
        // the delete path loses its preferred UUID lookup.
        log.info(
            "TelemetryProvider {} already exists in YBA with UUID {}, skipping creation",
            provider.getMetadata().getName(),
            existing.getUuid());
        OperatorUtils.maybeAddYbaResourceId(provider, existing.getUuid(), resourceClient);
        updateStatus(
            provider, STATE_READY, existing.getUuid().toString(), "Telemetry provider ready");
        return;
      }

      TelemetryProviderConfig config =
          crConverter.toConfig(
              provider,
              resourceTracker,
              KubernetesResourceDetails.fromResource(provider),
              operatorUtils.getLocalPlatformInstanceUuid().orElse(null));

      com.yugabyte.yw.models.TelemetryProvider ybaProvider =
          new com.yugabyte.yw.models.TelemetryProvider();
      ybaProvider.setCustomerUUID(cust.getUuid());
      ybaProvider.setName(providerName);
      ybaProvider.setConfig(config);
      Map<String, String> tags = provider.getSpec().getTags();
      ybaProvider.setTags(tags == null ? new HashMap<>() : new HashMap<>(tags));

      // Same gating and validation the REST create path applies, so that a disabled runtime flag or
      // an unreachable destination surfaces on the CR instead of leaving a broken provider behind.
      telemetryProviderService.throwExceptionIfRuntimeFlagDisabled();
      telemetryProviderService.throwRuntimeFlagDisabledForExporterTypeException(config.getType());
      telemetryProviderService.validateTelemetryProvider(ybaProvider);

      ybaProvider = telemetryProviderService.save(ybaProvider);
      UUID createdUuid = ybaProvider.getUuid();

      OperatorUtils.maybeAddYbaResourceId(provider, createdUuid, resourceClient);
      updateStatus(
          provider, STATE_READY, createdUuid.toString(), "Telemetry provider created successfully");
      log.info(
          "Successfully created TelemetryProvider {} with UUID: {}", providerName, createdUuid);
    } catch (Exception e) {
      log.error(
          "Failed to process TelemetryProvider {}: {}",
          provider.getMetadata().getName(),
          e.getMessage(),
          e);
      updateStatus(
          provider, STATE_ERROR, null, "Failed to create telemetry provider: " + e.getMessage());
    }
  }

  /**
   * Looks the telemetry provider up in YBA First by uuid - provided by the yba resource ID
   * annotation and then under the name from {@code metadata.name} - the same name a create would
   * use.
   *
   * @param provider the TelemetryProvider resource to look up
   * @param cust the operator customer the provider belongs to
   * @return the existing YBA provider, or null if it has not been created yet
   * @throws Exception if the lookup itself failed, so that an unknown answer is reported as an
   *     Error on the CR and retried, rather than risking a duplicate create
   */
  private com.yugabyte.yw.models.TelemetryProvider findExistingProvider(
      TelemetryProvider provider, Customer cust) throws Exception {
    UUID providerUUID = operatorUtils.getYbaResourceId(provider.getMetadata());
    if (providerUUID != null) {
      log.debug(
          "Looking up TelemetryProvider {} by UUID {}",
          provider.getMetadata().getName(),
          providerUUID);
      return telemetryProviderService.get(providerUUID);
    }
    log.debug(
        "Looking up TelemetryProvider {} by name {}",
        provider.getMetadata().getName(),
        getYbaProviderName(provider));
    return findByName(cust.getUuid(), getYbaProviderName(provider));
  }

  /**
   * Stamps the operator finalizer on the resource, so a delete leaves the object in place with its
   * deletionTimestamp set until the reconciler has removed the provider from YBA. Without it the
   * object is already gone when the delete event arrives, which would make the in-use refusal
   * unreportable and leak the YBA provider.
   */
  private void maybeAddFinalizer(TelemetryProvider provider) {
    ObjectMeta objectMeta = provider.getMetadata();
    List<String> current = objectMeta.getFinalizers();
    if (CollectionUtils.isNotEmpty(current) && current.contains(OperatorUtils.YB_FINALIZER)) {
      return;
    }
    List<String> updated = current == null ? new ArrayList<>() : new ArrayList<>(current);
    updated.add(OperatorUtils.YB_FINALIZER);
    objectMeta.setFinalizers(updated);
    try {
      resourceClient
          .inNamespace(objectMeta.getNamespace())
          .withName(objectMeta.getName())
          .patch(provider);
      log.debug("Added finalizer to TelemetryProvider {}", objectMeta.getName());
    } catch (Exception e) {
      // The object handed to the reconcile loop is the informer's cached copy, so a finalizer left
      // on it by a patch that never landed would make every later pass believe it is already set.
      objectMeta.setFinalizers(current);
      throw e;
    }
  }

  /**
   * Deletes the telemetry provider from YBA. The UUID recorded on the CR status is preferred, with
   * a lookup by name as fallback.
   *
   * @param provider the TelemetryProvider resource being deleted
   * @param cust the operator customer the provider belongs to
   * @return true if the provider is gone from YBA (deleted here, or never there), false if it is
   *     still present and the deletion has to be retried, with the reason recorded on the CR status
   * @throws Exception if the provider could not be resolved
   */
  private boolean processDeletion(TelemetryProvider provider, Customer cust) throws Exception {
    com.yugabyte.yw.models.TelemetryProvider ybaProvider = null;
    UUID providerUUID = getStatusResourceUUID(provider);
    if (providerUUID != null) {
      ybaProvider = telemetryProviderService.get(providerUUID);
      if (ybaProvider == null) {
        log.warn(
            "Telemetry provider UUID {} not found, trying to look it up by name", providerUUID);
      }
    }
    if (ybaProvider == null) {
      ybaProvider = findByName(cust.getUuid(), getYbaProviderName(provider));
    }
    if (ybaProvider == null) {
      log.info(
          "Telemetry provider for {} not found in YBA, nothing to delete",
          provider.getMetadata().getName());
      return true;
    }

    // YBA refuses to delete a telemetry provider that a universe still exports to (the REST delete
    // path rejects it), so make the same check here and report it on the CR rather than throwing.
    // The finalizer keeps the object alive, so InUse is observable and the requeued DELETE retries
    // the deletion once the universe stops exporting to the provider.
    if (telemetryProviderService.isProviderInUse(cust, ybaProvider.getUuid())) {
      String message =
          String.format(
              "Cannot delete telemetry provider '%s' (UUID: %s), as it is in use by a universe."
                  + " Stop exporting to it first.",
              ybaProvider.getName(), ybaProvider.getUuid());
      log.warn(message);
      updateStatus(provider, STATE_IN_USE, ybaProvider.getUuid().toString(), message);
      return false;
    }

    try {
      telemetryProviderService.delete(ybaProvider.getUuid());
      log.info(
          "Successfully deleted telemetry provider {} with UUID: {}",
          ybaProvider.getName(),
          ybaProvider.getUuid());
      return true;
    } catch (Exception e) {
      String message = "Failed to delete telemetry provider: " + e.getMessage();
      log.error(message, e);
      updateStatus(provider, STATE_ERROR, ybaProvider.getUuid().toString(), message);
      return false;
    }
  }

  private com.yugabyte.yw.models.TelemetryProvider findByName(UUID customerUUID, String name) {
    List<com.yugabyte.yw.models.TelemetryProvider> providers =
        telemetryProviderService.list(customerUUID, Collections.singleton(name));
    return providers.isEmpty() ? null : providers.get(0);
  }

  /** The name the provider is registered under in YBA */
  private static String getYbaProviderName(TelemetryProvider provider) {
    return provider.getMetadata().getName();
  }

  private static UUID getStatusResourceUUID(TelemetryProvider provider) {
    String resourceUUID =
        provider.getStatus() == null ? null : provider.getStatus().getResourceUUID();
    if (StringUtils.isBlank(resourceUUID)) {
      return null;
    }
    try {
      return UUID.fromString(resourceUUID);
    } catch (IllegalArgumentException e) {
      log.warn(
          "Invalid resourceUUID '{}' on TelemetryProvider {}, falling back to lookup by name",
          resourceUUID,
          provider.getMetadata().getName());
      return null;
    }
  }
}
