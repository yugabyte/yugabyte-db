// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.metrics;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.junit.Test;
import play.libs.Json;

public class UniverseMetricProviderTest extends FakeDBApplication {

  @Test
  public void testReadReplicaZonesGetThePrimaryOverrides() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    Pair<ObjectNode, List<AvailabilityZone>> details =
        ModelFactory.addClusterAndNodeDetailsK8s(
            "2024.2.0.0-b2",
            true /* createRR */,
            customer,
            Json.toJson(new DeviceInfo()).deepCopy());
    UniverseDefinitionTaskParams params =
        Json.fromJson(details.getFirst(), UniverseDefinitionTaskParams.class);
    UserIntent primaryIntent = params.getPrimaryCluster().userIntent;
    primaryIntent.universeOverrides = "resource:\n  tserver:\n    requests:\n      cpu: 3\n";
    Universe universe = ModelFactory.createUniverse("rr-zones", customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), u -> u.setUniverseDetails(params));

    Map<UUID, Map<String, Object>> overrides =
        UniverseMetricProvider.getFinalOverridesForAllZones(universe, primaryIntent);

    NodeDetails readReplicaTserver =
        params.nodeDetailsSet.stream()
            .filter(
                n ->
                    n.isTserver && n.placementUuid.equals(params.getReadOnlyClusters().get(0).uuid))
            .findFirst()
            .get();
    assertTrue(overrides.containsKey(readReplicaTserver.azUuid));
    assertEquals(
        3.0,
        KubernetesUtil.getCoreCountForUniverseForServer(
            params.getReadOnlyClusters().get(0).userIntent, overrides, readReplicaTserver),
        0);
  }

  @Test
  public void testEncryptionKeyExpiryDaysWithConfigsListedWithoutAuthConfig() {
    UUID customerUuid = UUID.randomUUID();
    long expiryMillis = System.currentTimeMillis() + TimeUnit.DAYS.toMillis(10) + 60_000;
    KmsConfig vaultConfig =
        KmsConfig.createKMSConfig(
            customerUuid,
            KeyProvider.HASHICORP,
            Json.newObject()
                .put(HashicorpVaultConfigParams.HC_VAULT_TTL, 3600L)
                .put(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY, expiryMillis),
            "vault");
    KmsConfig awsConfig =
        KmsConfig.createKMSConfig(
            customerUuid, KeyProvider.AWS, Json.newObject().put("key", "value"), "aws");
    Map<UUID, KmsConfig> configMap =
        KmsConfig.listAllKMSConfigsWithoutAuthConfig().stream()
            .collect(Collectors.toMap(KmsConfig::getConfigUUID, Function.identity()));

    KmsHistory vaultKey =
        KmsHistory.createKmsHistory(
            vaultConfig.getConfigUUID(),
            UUID.randomUUID(),
            KmsHistoryId.TargetType.UNIVERSE_KEY,
            "vault-key-ref",
            "vault-db-key-id");
    KmsHistory awsKey =
        KmsHistory.createKmsHistory(
            awsConfig.getConfigUUID(),
            UUID.randomUUID(),
            KmsHistoryId.TargetType.UNIVERSE_KEY,
            "aws-key-ref",
            "aws-db-key-id");

    assertEquals(10.0, UniverseMetricProvider.getEncryptionKeyExpiryDays(vaultKey, configMap), 0);
    // Only HashiCorp keys expire.
    assertNull(UniverseMetricProvider.getEncryptionKeyExpiryDays(awsKey, configMap));
  }
}
