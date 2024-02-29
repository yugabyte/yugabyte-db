package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.libs.Json.newObject;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.yb.CommonTypes;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;
import play.libs.Json;

public abstract class UniverseModifyBaseTest extends CommissionerBaseTest {
  protected static final String AZ_CODE = "az-1";

  protected Universe onPremUniverse;
  protected Universe defaultUniverse;

  protected AccessKey defaultAccessKey;

  protected Users defaultUser;

  protected ShellResponse dummyShellResponse;
  protected ShellResponse preflightResponse;

  protected YBClient mockClient;

  protected Hook hook1, hook2;
  protected HookScope hookScope1, hookScope2;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultAccessKey = createAccessKeyForProvider("default-key", defaultProvider);
    defaultUniverse = createUniverseForProvider("Test Universe", defaultProvider);
    onPremUniverse = createUniverseForProvider("Test onPrem Universe", onPremProvider);
    dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    preflightResponse = new ShellResponse();
    preflightResponse.message = "{\"test\": true}";
    AtomicInteger masterIpCnt = new AtomicInteger();
    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.Precheck)) {
                NodeTaskParams params = invocation.getArgument(1);
                assertTrue(StringUtils.isNotBlank((params.nodeName)));
                NodeInstance.getOrBadRequest(params.nodeUuid);
                return preflightResponse;
              }
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.List)) {
                ShellResponse listResponse = new ShellResponse();
                NodeTaskParams params = invocation.getArgument(1);
                ObjectNode respJson =
                    (ObjectNode)
                        Json.parse("{\"universe_uuid\":\"" + params.getUniverseUUID() + "\"}");
                Universe universe = Universe.getOrBadRequest(params.getUniverseUUID());
                NodeDetails nodeDetails = universe.getNode(params.nodeName);
                if (nodeDetails != null
                    && nodeDetails.dedicatedTo == UniverseTaskBase.ServerType.MASTER) {
                  respJson.put("private_ip", "10.0.0." + masterIpCnt.incrementAndGet());
                }
                if (params.nodeUuid != null) {
                  respJson.put("node_uuid", params.nodeUuid.toString());
                }
                listResponse.message = respJson.toString();
                return listResponse;
              }
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.Control)) {
                AnsibleClusterServerCtl.Params params = invocation.getArgument(1);
                assertTrue(StringUtils.isNotBlank(params.command));
                Universe universe = Universe.getOrBadRequest(params.getUniverseUUID());
                NodeDetails nodeDetails = universe.getNode(params.nodeName);
                if ("stop".equalsIgnoreCase(params.command)) {
                  assertFalse(nodeDetails.isSoftwareDeleted());
                }
              }
              return dummyShellResponse;
            });
    mockClient = mock(YBClient.class);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    doAnswer(
            inv -> {
              ObjectNode res = newObject();
              res.put("response", "success");
              return res;
            })
        .when(mockYsqlQueryExecutor)
        .executeQueryInNodeShell(any(), any(), any(), anyBoolean());
    // Create hooks
    hook1 =
        Hook.create(
            defaultCustomer.getUuid(),
            "hook1",
            Hook.ExecutionLang.Python,
            "HOOK\nTEXT\n",
            true,
            null);
    hook2 =
        Hook.create(
            defaultCustomer.getUuid(),
            "hook2",
            Hook.ExecutionLang.Bash,
            "HOOK\nTEXT\n",
            true,
            null);
    hookScope1 =
        HookScope.create(defaultCustomer.getUuid(), HookScope.TriggerType.PreNodeProvision);
    hookScope2 =
        HookScope.create(defaultCustomer.getUuid(), HookScope.TriggerType.PostNodeProvision);
    hookScope1.addHook(hook1);
    hookScope2.addHook(hook2);
  }

  protected Universe createUniverseForProvider(String universeName, Provider provider) {
    Region region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone zone = AvailabilityZone.createOrThrow(region, AZ_CODE, "AZ 1", "subnet-1");
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "default-key";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    Common.CloudType providerType = Common.CloudType.valueOf(provider.getCode());
    userIntent.providerType = providerType;
    userIntent.provider = provider.getUuid().toString();
    userIntent.universeName = universeName;
    if (providerType == Common.CloudType.onprem) {
      createOnpremInstance(zone);
      createOnpremInstance(zone);
      createOnpremInstance(zone);
    }
    Map<String, String> gflags = new HashMap<>();
    gflags.put("foo", "bar");
    userIntent.masterGFlags = gflags;
    userIntent.tserverGFlags = gflags;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    Universe result = createUniverse(universeName, defaultCustomer.getId(), providerType);
    result =
        Universe.saveDetails(
            result.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    if (providerType == Common.CloudType.onprem) {
      Universe.saveDetails(
          result.getUniverseUUID(),
          u -> {
            String instanceType = u.getNodes().iterator().next().cloudInfo.instance_type;
            Map<UUID, Set<String>> onpremAzToNodes = new HashMap<>();
            for (NodeDetails node : u.getNodes()) {
              Set<String> nodeNames = onpremAzToNodes.getOrDefault(node.azUuid, new HashSet<>());
              nodeNames.add(node.nodeName);
              onpremAzToNodes.put(node.azUuid, nodeNames);
            }
            Cluster primaryCluster = u.getUniverseDetails().getPrimaryCluster();
            Map<String, NodeInstance> nodeMap =
                NodeInstance.pickNodes(primaryCluster.uuid, onpremAzToNodes, instanceType);
            for (NodeDetails node : u.getNodes()) {
              NodeInstance nodeInstance = nodeMap.get(node.nodeName);
              if (nodeInstance != null) {
                node.nodeUuid = nodeInstance.getNodeUuid();
              }
            }
          },
          false);
    }

    return result;
  }

  protected NodeInstance createOnpremInstance(AvailabilityZone zone) {
    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
    nodeData.ip = "fake_ip_" + zone.getRegion().getCode();
    nodeData.region = zone.getRegion().getCode();
    nodeData.zone = zone.getCode();
    nodeData.instanceType = ApiUtils.UTIL_INST_TYPE;
    return NodeInstance.create(zone.getUuid(), nodeData);
  }

  protected Universe createUniverseForProviderWithReadReplica(
      String universeName, Provider provider) {
    Universe universe = createUniverseForProvider(universeName, provider);
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    // Adding Read Replica cluster.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "default-key";
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.regionList = new ArrayList<>(primaryCluster.userIntent.regionList);
    userIntent.enableYSQL = true;
    Common.CloudType providerType = Common.CloudType.valueOf(provider.getCode());
    userIntent.providerType = providerType;
    userIntent.provider = provider.getUuid().toString();
    userIntent.universeName = universeName;

    Region region = Region.getByProvider(provider.getUuid()).get(0);
    PlacementInfo pi = new PlacementInfo();
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(region, "az-4", "AZ 4", "subnet-1");
    AvailabilityZone az5 = AvailabilityZone.createOrThrow(region, "az-5", "AZ 5", "subnet-2");
    AvailabilityZone az6 = AvailabilityZone.createOrThrow(region, "az-6", "AZ 6", "subnet-3");
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az5.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az6.getUuid(), pi, 1, 1, false);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    return universe;
  }

  protected AccessKey createAccessKeyForProvider(String keyCode, Provider provider) {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.sshUser = "ssh_user";
    keyInfo.sshPort = 22;
    AccessKey accessKey = AccessKey.create(provider.getUuid(), keyCode, keyInfo);
    return accessKey;
  }

  public static void mockMasterAndPeerRoles(YBClient client, List<String> masters) {
    mockMasterAndPeerRoles(client, masters.isEmpty() ? null : masters.get(0), masters);
  }

  public static void mockMasterAndPeerRoles(
      YBClient client, String masterLeaderIp, List<String> masters) {
    when(client.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost(masterLeaderIp));
    try {
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);

      List<ServerInfo> serverInfos =
          masters.stream()
              .map(
                  m ->
                      new ServerInfo(
                          UUID.randomUUID().toString(),
                          m,
                          0,
                          m.equals(masterLeaderIp),
                          "OK",
                          m.equals(masterLeaderIp)
                              ? CommonTypes.PeerRole.LEADER
                              : CommonTypes.PeerRole.FOLLOWER))
              .collect(Collectors.toList());
      when(listMastersResponse.getMasters()).thenReturn(serverInfos);
      when(client.listMasters()).thenReturn(listMastersResponse);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public void mockGetMasterRegistrationResponse(List<String> addedIps, List<String> removedIps) {
    mockGetMasterRegistrationResponse(mockClient, addedIps, removedIps);
  }

  public static void mockGetMasterRegistrationResponse(
      YBClient client, List<String> addedIps, List<String> removedIps) {
    /* reimplement once correct RPC method is used
    List<GetMasterRegistrationResponse> responses = new ArrayList<>();
    responses.addAll(
        addedIps.stream()
            .map(
                ip ->
                    new GetMasterRegistrationResponse(
                        5,
                        "",
                        CommonTypes.PeerRole.FOLLOWER,
                        WireProtocol.ServerRegistrationPB.newBuilder()
                            .addPrivateRpcAddresses(
                                CommonNet.HostPortPB.newBuilder().setHost(ip).setPort(7100).build())
                            .build(),
                        null))
            .toList());
    responses.addAll(
        removedIps.stream()
            .map(
                ip ->
                    new GetMasterRegistrationResponse(
                        5,
                        "",
                        CommonTypes.PeerRole.NON_PARTICIPANT,
                        WireProtocol.ServerRegistrationPB.newBuilder()
                            .addPrivateRpcAddresses(
                                CommonNet.HostPortPB.newBuilder().setHost(ip).setPort(7100).build())
                            .build(),
                        null))
            .toList());
    when(client.getMasterRegistrationResponseList()).thenReturn(responses);*/
  }
}
