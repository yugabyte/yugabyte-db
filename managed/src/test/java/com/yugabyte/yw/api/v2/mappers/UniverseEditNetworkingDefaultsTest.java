package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.UniverseDefinitionTaskParamsMapper;
import api.v2.models.UniverseEditSpec;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import java.util.ArrayList;
import java.util.UUID;
import org.junit.Test;

public class UniverseEditNetworkingDefaultsTest {
  private final ObjectMapper mapper = new ObjectMapper();

  @Test
  public void omittedNetworkingFieldsDoNotApplySchemaDefaults() throws Exception {
    UUID clusterUuid = UUID.randomUUID();
    UniverseDefinitionTaskParams existing = universeWithNonDefaultNetworking(clusterUuid);

    UniverseEditSpec edit =
        mapper.readValue(
            "{"
                + "\"expected_universe_version\": -1,"
                + "\"clusters\": [{\"uuid\": \""
                + clusterUuid
                + "\"}],"
                + "\"networking_spec\": {\"communication_ports\": {\"ysql_server_rpc_port\": 5444}},"
                + "\"universe_settings\": {}"
                + "}",
            UniverseEditSpec.class);

    assertNull(edit.getNetworkingSpec().getAssignPublicIp());
    assertNull(edit.getNetworkingSpec().getAssignStaticPublicIp());
    assertNull(edit.getNetworkingSpec().getCommunicationPorts().getMasterHttpPort());
    assertNull(edit.getUniverseSettings().getExpertMode());

    UniverseDefinitionTaskParams mapped =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV1UniverseDefinitionTaskParamsFromEditSpec(
            edit, existing);

    UserIntent intent = mapped.getPrimaryCluster().userIntent;
    assertFalse(intent.assignPublicIP);
    assertTrue(intent.assignStaticPublicIP);
    assertTrue(intent.enableIPV6);
    assertEquals(7001, mapped.communicationPorts.masterHttpPort);
    assertEquals(5444, mapped.communicationPorts.ysqlServerRpcPort);
    assertEquals(7100, mapped.communicationPorts.masterRpcPort);
    assertTrue(mapped.universeSettings.isExpertMode());
  }

  @Test
  public void explicitNetworkingFieldsAreApplied() throws Exception {
    UUID clusterUuid = UUID.randomUUID();
    UniverseDefinitionTaskParams existing = universeWithNonDefaultNetworking(clusterUuid);

    UniverseEditSpec edit =
        mapper.readValue(
            "{"
                + "\"expected_universe_version\": -1,"
                + "\"clusters\": [{\"uuid\": \""
                + clusterUuid
                + "\"}],"
                + "\"networking_spec\": {"
                + "\"assign_public_ip\": true,"
                + "\"assign_static_public_ip\": false,"
                + "\"enable_ipv6\": false"
                + "},"
                + "\"universe_settings\": {\"expert_mode\": false}"
                + "}",
            UniverseEditSpec.class);

    UniverseDefinitionTaskParams mapped =
        UniverseDefinitionTaskParamsMapper.INSTANCE.toV1UniverseDefinitionTaskParamsFromEditSpec(
            edit, existing);

    UserIntent intent = mapped.getPrimaryCluster().userIntent;
    assertTrue(intent.assignPublicIP);
    assertFalse(intent.assignStaticPublicIP);
    assertFalse(intent.enableIPV6);
    assertEquals(7001, mapped.communicationPorts.masterHttpPort);
    assertFalse(mapped.universeSettings.isExpertMode());
  }

  private static UniverseDefinitionTaskParams universeWithNonDefaultNetworking(UUID clusterUuid) {
    UserIntent intent = new UserIntent();
    intent.assignPublicIP = false;
    intent.assignStaticPublicIP = true;
    intent.enableIPV6 = true;

    Cluster cluster = new Cluster();
    cluster.uuid = clusterUuid;
    cluster.clusterType = UniverseDefinitionTaskParams.ClusterType.PRIMARY;
    cluster.userIntent = intent;

    UniverseDefinitionTaskParams existing = new UniverseDefinitionTaskParams();
    existing.clusters = new ArrayList<>();
    existing.clusters.add(cluster);
    existing.communicationPorts = new CommunicationPorts();
    existing.communicationPorts.masterHttpPort = 7001;
    existing.universeSettings = new UniverseDefinitionTaskParams.UniverseSettings();
    existing.universeSettings.setExpertMode(true);
    return existing;
  }
}
