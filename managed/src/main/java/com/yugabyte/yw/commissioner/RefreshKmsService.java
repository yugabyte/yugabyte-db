package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Duration;
import java.util.Base64;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class RefreshKmsService {

  private final PlatformScheduler platformScheduler;
  private final EncryptionAtRestManager encryptionAtRestManager;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final MetricService metricService;
  // In hours
  private final String KMS_REFRESH_INTERVAL_PATH = "yb.kms.refresh_interval";

  @Inject
  public RefreshKmsService(
      PlatformScheduler platformScheduler,
      EncryptionAtRestManager encryptionAtRestManager,
      RuntimeConfigFactory runtimeConfigFactory,
      MetricService metricService) {
    this.platformScheduler = platformScheduler;
    this.encryptionAtRestManager = encryptionAtRestManager;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.metricService = metricService;
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, getRefreshInterval(), this::scheduleRunner);
  }

  private Duration getRefreshInterval() {
    return runtimeConfigFactory.globalRuntimeConf().getDuration(KMS_REFRESH_INTERVAL_PATH);
  }

  private void refreshAllKmsConfigs(Customer customer) {
    // List all the KMS configs to validate.
    List<KmsConfig> kmsConfigs = KmsConfig.listKMSConfigs(customer.getUuid());

    // Try to validate the active KMS config on all universes for the customer.
    for (Universe universe : customer.getUniverses()) {
      try {
        UUID universeUUID = universe.getUniverseUUID();
        KmsHistory activeKmsHistory = EncryptionAtRestUtil.getActiveKey(universeUUID);
        int numKeys = EncryptionAtRestUtil.getNumUniverseKeys(universeUUID);

        // If there is no active key, we don't need to validate the KMS config on the universe.
        Boolean isEarEnabled =
            universe.getUniverseDetails().encryptionAtRestConfig.encryptionAtRestEnabled;
        if (isEarEnabled && numKeys > 0 && activeKmsHistory != null) {
          KmsConfig kmsConfig = KmsConfig.getOrBadRequest(activeKmsHistory.getConfigUuid());

          // Remove from the KmsConfig list to check later, since we will check this now.
          kmsConfigs.removeIf(kc -> kc.getConfigUUID().equals(kmsConfig.getConfigUUID()));

          // Get the current active encrypted universe key to try to decrypt below.
          byte[] keyRef = Base64.getDecoder().decode(activeKmsHistory.getUuid().keyRef);
          byte[] decryptedKey = null;
          try {
            // Check if we are able to decrypt the current active universe key as a validation step.
            log.debug(
                "RefreshKmsService: Validating last active key with KMS config '{}' for universe"
                    + " '{}'.",
                kmsConfig.getConfigUUID(),
                universeUUID);
            decryptedKey =
                encryptionAtRestManager
                    .getServiceInstance(kmsConfig.getKeyProvider().name())
                    .validateConfigForUpdate(
                        universeUUID,
                        kmsConfig.getConfigUUID(),
                        keyRef,
                        activeKmsHistory.encryptionContext,
                        universe.getUniverseDetails().encryptionAtRestConfig,
                        kmsConfig.getAuthConfig());

            // Check if the KMS config is valid and has valid settings.
            log.debug(
                "RefreshKmsService: Validating settings of KMS config '{}' for universe '{}'.",
                kmsConfig.getConfigUUID(),
                universeUUID);
            encryptionAtRestManager
                .getServiceInstance(kmsConfig.getKeyProvider().name())
                .refreshKms(kmsConfig.getConfigUUID());
          } catch (Exception e) {
            String errMsg =
                String.format(
                    "Error validating the active KMS History with KMS config '%s' for universe"
                        + " '%s'.",
                    kmsConfig.getConfigUUID(), universeUUID);
            log.error(errMsg, e);
            decryptedKey = null;
          }
          if (decryptedKey == null) {
            String errMsg =
                String.format(
                    "Error validating the active KMS History with KMS config '%s' for universe '%s'"
                        + " before generating universe key. Possibly the master key is recreated"
                        + " with the same name or has invalid settings to decrypt the active"
                        + " universe key.",
                    kmsConfig.getConfigUUID(), universeUUID);
            log.error(errMsg);
            // Raise a metric to indicate the failure of the KMS config validation.
            metricService.setFailureStatusMetric(
                MetricService.buildMetricTemplate(PlatformMetrics.UNIVERSE_KMS_KEY_STATUS, universe)
                    .setLabel(KnownAlertLabels.KMS_CONFIG_NAME, kmsConfig.getName()));
          } else {
            log.info(
                String.format(
                    "Successfully validated the active KMS History with KMS config '%s' for"
                        + " universe '%s'.",
                    kmsConfig.getConfigUUID(), universeUUID));
            // Raise a metric to indicate the success of the KMS config validation.
            metricService.setOkStatusMetric(
                MetricService.buildMetricTemplate(PlatformMetrics.UNIVERSE_KMS_KEY_STATUS, universe)
                    .setLabel(KnownAlertLabels.KMS_CONFIG_NAME, kmsConfig.getName()));
          }
        }
      } catch (Exception e) {
        log.error(
            String.format(
                "Error running KMS Refresh Service on Universe '%s'.", universe.getName()),
            e);
      }
    }

    // Now check the remaining KMS configs that are not active on a universe.
    // We will not raise a metric or alert for these configs, but we will try to refresh them.
    // This is useful for KMS configs that are not used by any universe, but we still want to
    // refresh them to ensure they are valid and can be used in the future.
    for (KmsConfig kmsConfig : kmsConfigs) {
      try {
        // Only do a refresh if the KMS config is not active on any universe.
        // Unlike the above case, we will not try to also decrypt any active universe key.
        encryptionAtRestManager
            .getServiceInstance(kmsConfig.getKeyProvider().name())
            .refreshKms(kmsConfig.getConfigUUID());
      } catch (Exception e) {
        log.error(
            String.format(
                "Error running KMS Refresh Service on KMS config '%s'.", kmsConfig.getName()),
            e);
      }
    }
  }

  @VisibleForTesting
  void scheduleRunner() {
    log.info("Running KMS Refresh Service.");
    try {
      Customer.getAll()
          .forEach(
              c -> {
                refreshAllKmsConfigs(c);
              });
      log.info("Finished KMS Refresh Service.");
    } catch (Exception e) {
      log.error("Error running KMS Refresh Service.", e);
    }
  }
}
