// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Webhook;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateDrConfigParams extends XClusterConfigTaskBase {

  @Inject
  protected UpdateDrConfigParams(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends DrConfigTaskParams {

    public UUID drConfigUUID;

    // XClusterConfigCreateFormData.BootstrapParams bootstrapParams in re-used.

    // DrConfigCreateForm.PitrParams pitrParams is re-used.

    // webhookParams is re-used.
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(drConfig=%s,bootstrapParams=%s,pitrParams=%s,webhookUrls=%s)",
        super.getName(),
        taskParams().drConfigUUID,
        taskParams().getBootstrapParams(),
        taskParams().getPitrParams(),
        taskParams().getWebhookUrls());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    try {

      DrConfig drConfig = DrConfig.getOrBadRequest(taskParams().drConfigUUID);

      if (taskParams().getBootstrapParams() != null) {
        log.info(
            "Updating DR config {} with bootstrap params: {}",
            taskParams().drConfigUUID,
            taskParams().getBootstrapParams());
        drConfig.setStorageConfigUuid(
            taskParams().getBootstrapParams().backupRequestParams.storageConfigUUID);
        drConfig.setParallelism(taskParams().getBootstrapParams().backupRequestParams.parallelism);
        drConfig.update();
      }

      if (taskParams().getPitrParams() != null) {
        log.info(
            "Updating DR config {} with pitr params: {}",
            taskParams().drConfigUUID,
            taskParams().getPitrParams());
        drConfig.setPitrRetentionPeriodSec(taskParams().getPitrParams().retentionPeriodSec);
        drConfig.setPitrSnapshotIntervalSec(taskParams().getPitrParams().snapshotIntervalSec);
        drConfig.update();
      }

      if (taskParams().getWebhookUrls() != null) {
        for (Webhook oldWebhook : drConfig.getWebhooks()) {
          oldWebhook.delete();
        }
        List<Webhook> newWebhooks = new ArrayList<>();
        for (String webhookUrl : taskParams().getWebhookUrls()) {
          Webhook newWebhook = new Webhook(drConfig, webhookUrl);
          newWebhooks.add(newWebhook);
        }
        drConfig.setWebhooks(newWebhooks);
        drConfig.update();
      }

    } catch (Exception exception) {
      log.error("hit error: ", exception);
      throw new RuntimeException(exception);
    }

    log.info("Completed {}", getName());
  }
}
