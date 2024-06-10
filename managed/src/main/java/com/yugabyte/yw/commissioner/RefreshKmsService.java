package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import java.time.Duration;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class RefreshKmsService {

  private final PlatformScheduler platformScheduler;
  private final EncryptionAtRestManager encryptionAtRestManager;
  private final RuntimeConfigFactory runtimeConfigFactory;
  // In hours
  private final String KMS_REFRESH_INTERVAL_PATH = "yb.kms.refresh_interval";

  @Inject
  public RefreshKmsService(
      PlatformScheduler platformScheduler,
      EncryptionAtRestManager encryptionAtRestManager,
      RuntimeConfigFactory runtimeConfigFactory) {
    this.platformScheduler = platformScheduler;
    this.encryptionAtRestManager = encryptionAtRestManager;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, getRefreshInterval(), this::scheduleRunner);
  }

  private Duration getRefreshInterval() {
    return runtimeConfigFactory.globalRuntimeConf().getDuration(KMS_REFRESH_INTERVAL_PATH);
  }

  private void refreshAllKmsConfigs(Customer customer) {
    List<KmsConfig> kmsConfigs = KmsConfig.listKMSConfigs(customer.getUuid());
    for (KmsConfig kmsConfig : kmsConfigs) {
      try {
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
    } catch (Exception e) {
      log.error("Error running KMS Refresh Service.", e);
    }
  }
}
