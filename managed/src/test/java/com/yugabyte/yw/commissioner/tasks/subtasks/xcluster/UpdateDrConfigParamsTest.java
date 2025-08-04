// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Webhook;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;

public class UpdateDrConfigParamsTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe sourceUniverse;
  private Universe targetUniverse;
  private UpdateDrConfigParams.Params params;
  private UpdateDrConfigParams task;
  private DrConfig drConfig;

  @Before
  public void setUp() throws IOException, NoSuchAlgorithmException {
    defaultCustomer = ModelFactory.testCustomer();
    sourceUniverse = ModelFactory.createUniverse("source-universe", defaultCustomer.getId());
    targetUniverse = ModelFactory.createUniverse("target-universe", defaultCustomer.getId());
    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(defaultCustomer, "test_nfs_storage");
    XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams storageParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    storageParams.storageConfigUUID = customerConfig.getConfigUUID();
    DrConfigCreateForm.PitrParams pitrParams = new DrConfigCreateForm.PitrParams();
    pitrParams.retentionPeriodSec = 5;
    pitrParams.snapshotIntervalSec = 1;

    // Create dr config between source and target universe.
    drConfig =
        DrConfig.create(
            "test-dr-name",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            storageParams,
            pitrParams,
            new HashSet<>(Set.of("000000000000", "11111111")),
            false);

    task = AbstractTaskBase.createTask(UpdateDrConfigParams.class);
    params = new UpdateDrConfigParams.Params();
    params.drConfigUUID = drConfig.getUuid();
  }

  @Test
  public void testUpdateWebhookUrls() {
    List<String> webhookUrls =
        new ArrayList<>(Arrays.asList("http://localhost:9090", "https://portal."));
    params.setWebhookUrls(webhookUrls);
    task.initialize(params);
    task.run();
    drConfig.refresh();

    assertEquals(2, drConfig.getWebhooks().size());
    for (Webhook webhook : drConfig.getWebhooks()) {
      assertTrue(webhookUrls.stream().anyMatch(url -> url.equals(webhook.getUrl())));
    }
  }
}
