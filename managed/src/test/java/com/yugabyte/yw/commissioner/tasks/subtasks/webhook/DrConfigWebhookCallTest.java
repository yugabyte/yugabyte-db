// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.webhook;

import static org.junit.Assert.assertEquals;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.UpdateDrConfigParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class DrConfigWebhookCallTest extends FakeDBApplication {

  private static final String WEBHOOK_TEST_PATH = "/here/is/path";

  private Customer defaultCustomer;
  private Universe sourceUniverse;
  private Universe targetUniverse;
  private UpdateDrConfigParams.Params params;
  private UpdateDrConfigParams task;
  private DrConfig drConfig;
  private List<String> nodeIps = new ArrayList<>(Arrays.asList("1.2.3.4", "1.2.3.5"));

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    sourceUniverse = ModelFactory.createUniverse("source-universe", defaultCustomer.getId());
    targetUniverse = ModelFactory.createUniverse("target-universe", defaultCustomer.getId());

    UniverseDefinitionTaskParams details = sourceUniverse.getUniverseDetails();
    for (String ip : nodeIps) {
      NodeDetails node = new NodeDetails();
      node.cloudInfo = new CloudSpecificInfo();
      node.cloudInfo.private_ip = ip;
      details.nodeDetailsSet.add(node);
    }
    sourceUniverse.setUniverseDetails(details);
    sourceUniverse.update();

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
            new HashSet<>(Set.of("000000000000", "11111111")));
  }

  private void addWebhooks(List<String> webhookUrls) {
    task = AbstractTaskBase.createTask(UpdateDrConfigParams.class);
    params = new UpdateDrConfigParams.Params();
    params.drConfigUUID = drConfig.getUuid();
    params.setWebhookUrls(webhookUrls);
    task.initialize(params);
    task.run();
    drConfig.refresh();
  }

  // Since the webhook doesn't throw error on failure. We need to be able to timeout,
  //   otherwise takeRequest() will hang.
  @Test(timeout = 120000)
  public void testSuccessfulWebhookCall() throws IOException, InterruptedException {
    try (MockWebServer server = new MockWebServer()) {
      server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));
      server.start();
      HttpUrl baseUrl = server.url(WEBHOOK_TEST_PATH);
      addWebhooks(new ArrayList<>(Arrays.asList(baseUrl.toString())));

      DrConfigWebhookCall webhookTask = AbstractTaskBase.createTask(DrConfigWebhookCall.class);
      DrConfigWebhookCall.Params webhookParams = new DrConfigWebhookCall.Params();
      webhookParams.drConfigUuid = drConfig.getUuid();
      webhookParams.hook = drConfig.getWebhooks().get(0);
      webhookTask.initialize(webhookParams);
      webhookTask.run();

      RecordedRequest request = server.takeRequest();
      assertEquals(WEBHOOK_TEST_PATH, request.getPath());
      String requestBody = request.getBody().readString(Charset.defaultCharset());
      JsonNode requestJson = Json.parse(requestBody);
      assertEquals("A", requestJson.get("recordType").asText());
      assertEquals(drConfig.getUuid().toString(), requestJson.get("drConfigUuid").asText());
      assertEquals("5", requestJson.get("ttl").asText());
      assertEquals(String.join(",", nodeIps), requestJson.get("ips").asText());
    }
  }
}
