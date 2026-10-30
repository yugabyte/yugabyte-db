package com.yugabyte.yw.cloud.gcp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.api.services.compute.model.Address;
import com.google.api.services.compute.model.Backend;
import com.google.api.services.compute.model.BackendService;
import com.google.api.services.compute.model.ForwardingRule;
import com.google.api.services.compute.model.HealthCheck;
import com.google.api.services.compute.model.InstanceGroup;
import com.google.api.services.compute.model.InstanceReference;
import com.google.api.services.compute.model.TCPHealthCheck;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.InOrder;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;
import org.mockito.Spy;

public class GCPCloudImplTest extends FakeDBApplication {

  @Spy @InjectMocks GCPCloudImpl gcpCloudImpl;
  private Customer customer;
  private Provider defaultProvider;
  private Region defaultRegion;
  @Mock private GCPProjectApiClient mockApiClient;
  @Mock RuntimeConfGetter mockConfGetter;
  private String GCPBaseUrl = "https://compute.googleapis.com/compute/v1/projects/project/";

  @Before
  public void setup() throws Exception {
    MockitoAnnotations.openMocks(this);
    when(mockConfGetter.getConfForScope(
            any(Provider.class), eq(ProviderConfKeys.gcpConnectionDrainingTimeout)))
        .thenReturn(Duration.ofMinutes(5));
    customer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.gcpProvider(customer);
    defaultRegion = new Region();
    defaultRegion.setProvider(defaultProvider);
    defaultRegion.setName("us-west1");
    defaultRegion.setCode("us-west1");
    AvailabilityZone az = new AvailabilityZone();
    az.setCode("us-west1-a");
    defaultRegion.setZones(Arrays.asList(az));
    defaultProvider.getRegions().add(defaultRegion);
    ProviderDetails providerDetails = new ProviderDetails();
    CloudInfo cloudInfo = new CloudInfo();
    cloudInfo.gcp = new GCPCloudInfo();
    cloudInfo.gcp.setGceProject("project");
    providerDetails.setCloudInfo(cloudInfo);
    defaultProvider.setDetails(providerDetails);
    mockApiClient = mock(GCPProjectApiClient.class);
  }

  @Test
  public void testCreateNewBackends() throws Exception {
    String instanceGroupUrl = GCPBaseUrl + "zones/us-west1-a/instanceGroups/" + IG_NAME;
    String instanceName = UUID.randomUUID().toString();
    String instanceUrl = GCPBaseUrl + "zones/us-west1-a/instances/" + instanceName;
    InstanceReference instance = new InstanceReference();
    instance.setInstance(instanceUrl);
    List<InstanceReference> instances = Arrays.asList(instance);
    Map<String, List<InstanceReference>> nodeAzMap = new HashMap();
    nodeAzMap.put("us-west1-a", instances);
    when(mockApiClient.createNewInstanceGroupInZone("us-west1-a", IG_NAME))
        .thenReturn(instanceGroupUrl);
    List<Backend> newBakcends = gcpCloudImpl.createNewBackends(mockApiClient, LB_NAME, nodeAzMap);
    // Assert that exactly 1 new backend was created
    assertEquals(newBakcends.size(), 1);
    // Assert that the backend created has the correct name
    assertEquals(newBakcends.get(0).getGroup(), instanceGroupUrl);
    verify(mockApiClient).addInstancesToInstaceGroup("us-west1-a", IG_NAME, instances);
  }

  @Test
  public void testCreateNewBackendsReusesExistingGroupAndSyncsItsInstances() throws Exception {
    String groupUrl = GCPBaseUrl + "zones/us-west1-a/instanceGroups/" + IG_NAME;
    InstanceReference kept = instanceRef("us-west1-a", "n1");
    InstanceReference added = instanceRef("us-west1-a", "n2");
    InstanceReference stale = instanceRef("us-west1-a", "n0");
    when(mockApiClient.getInstanceGroupIfExists("us-west1-a", IG_NAME))
        .thenReturn(new InstanceGroup().setSelfLink(groupUrl));
    when(mockApiClient.getInstancesForInstanceGroup("us-west1-a", IG_NAME))
        .thenReturn(List.of(kept, stale));

    List<Backend> backends =
        gcpCloudImpl.createNewBackends(
            mockApiClient, LB_NAME, Map.of("us-west1-a", List.of(kept, added)));

    assertEquals(1, backends.size());
    assertEquals(groupUrl, backends.get(0).getGroup());
    verify(mockApiClient, never()).createNewInstanceGroupInZone(any(), any());
    verify(mockApiClient).addInstancesToInstaceGroup("us-west1-a", IG_NAME, List.of(added));
    verify(mockApiClient).removeInstancesFromInstaceGroup("us-west1-a", IG_NAME, List.of(stale));
  }

  private InstanceReference instanceRef(String zone, String name) {
    return new InstanceReference().setInstance(GCPBaseUrl + "zones/" + zone + "/instances/" + name);
  }

  @Test
  public void testGetAzToInstanceReferenceMap() {
    AvailabilityZone az1 = new AvailabilityZone();
    AvailabilityZone az2 = new AvailabilityZone();
    az1.setName("us-west1-a");
    az2.setName("us-west1-b");
    InstanceReference instance1 = new InstanceReference();
    InstanceReference instance2 = new InstanceReference();
    String instance1Name = UUID.randomUUID().toString();
    String instance1Url = GCPBaseUrl + "zones/" + az1.getName() + "/instances/" + instance1Name;
    instance1.setInstance(instance1Url);
    String instance2Name = UUID.randomUUID().toString();
    String instance2Url = GCPBaseUrl + "zones/" + az2.getName() + "/instances/" + instance2Name;
    instance2.setInstance(instance2Url);
    NodeID node1Id = new NodeID(instance1Name, instance1Name);
    NodeID node2Id = new NodeID(instance2Name, instance2Name);
    Map<AvailabilityZone, Set<NodeID>> azToNodeIdMap = new HashMap();
    Set<NodeID> set1 = new HashSet();
    set1.add(node1Id);
    azToNodeIdMap.put(az1, set1);
    Set<NodeID> set2 = new HashSet();
    set2.add(node2Id);
    azToNodeIdMap.put(az2, set2);
    Map<String, List<InstanceReference>> azToInstanceReferenceMap = new HashMap();
    azToInstanceReferenceMap.put(az1.getName(), Arrays.asList(instance1));
    azToInstanceReferenceMap.put(az2.getName(), Arrays.asList(instance2));
    when(mockApiClient.getInstancesInZoneByNames(eq(az1.getName()), any()))
        .thenReturn(Arrays.asList(instance1));
    when(mockApiClient.getInstancesInZoneByNames(eq(az2.getName()), any()))
        .thenReturn(Arrays.asList(instance2));
    Map<String, List<InstanceReference>> result =
        gcpCloudImpl.getAzToInstanceReferenceMap(mockApiClient, azToNodeIdMap);
    assertEquals(azToInstanceReferenceMap.entrySet(), result.entrySet());
  }

  @Test
  public void testEnsureForwardingRulesSucess() {
    List<Integer> portsToCheck = new ArrayList();
    portsToCheck.add(5433);
    List<ForwardingRule> forwardingRules = new ArrayList();
    ForwardingRule forwardingRule = new ForwardingRule();
    forwardingRule.setIPProtocol("TCP");
    forwardingRule.setPorts(Arrays.asList("5433"));
    forwardingRules.add(forwardingRule);
    gcpCloudImpl.ensureForwardingRules("TCP", portsToCheck, forwardingRules);
  }

  @Test
  public void testEnsureForwardingRulesFailure() {
    List<Integer> portsToCheck = Arrays.asList(5433);
    List<ForwardingRule> forwardingRules = new ArrayList();
    ForwardingRule forwardingRule = new ForwardingRule();
    forwardingRule.setIPProtocol("TCP");
    forwardingRule.setPorts(Arrays.asList("123"));
    forwardingRules.add(forwardingRule);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> gcpCloudImpl.ensureForwardingRules("TCP", portsToCheck, forwardingRules));
    assert (exception.getMessage().contains("Forwarding rule missing for some ports: "));
  }

  @Test
  public void testEnsureHealthChecksEmpty() throws Exception {
    String region = "us-west1";
    String protocol = "TCP";
    int port = 5433;
    NLBHealthCheckConfiguration healthCheckConfiguration =
        new NLBHealthCheckConfiguration(Arrays.asList(port), Protocol.TCP, new ArrayList<>());
    String helathCheckName = UUID.randomUUID().toString();
    String healthCheckUrl = GCPBaseUrl + "/regions/" + region + "/healthChecks/" + helathCheckName;
    List<String> healthCheckUrls = new ArrayList();
    healthCheckUrls.add(healthCheckUrl);
    when(mockApiClient.createNewTCPHealthCheckForPort(region, HC_TCP_NAME, 5433))
        .thenReturn(healthCheckUrl);
    List<String> finalHealthChecks =
        gcpCloudImpl.ensureHealthChecks(
            mockApiClient, region, LB_NAME, healthCheckConfiguration, new ArrayList<String>());
    assertEquals(1, finalHealthChecks.size());
    assertEquals(healthCheckUrl, finalHealthChecks.get(0));
  }

  @Test
  public void testEnsureHealthChecksNull() throws Exception {
    String region = "us-west1";
    String protocol = "TCP";
    int port = 5433;
    NLBHealthCheckConfiguration healthCheckConfiguration =
        new NLBHealthCheckConfiguration(Arrays.asList(port), Protocol.TCP, Arrays.asList());
    String helathCheckName = UUID.randomUUID().toString();
    String healthCheckUrl = GCPBaseUrl + "/regions/" + region + "/healthChecks/" + helathCheckName;
    when(mockApiClient.createNewTCPHealthCheckForPort(anyString(), anyString(), eq(5433)))
        .thenReturn(healthCheckUrl);
    List<String> finalHealthChecks =
        gcpCloudImpl.ensureHealthChecks(
            mockApiClient, region, LB_NAME, healthCheckConfiguration, null);
    assertEquals(1, finalHealthChecks.size());
    assertEquals(healthCheckUrl, finalHealthChecks.get(0));
  }

  @Test
  public void testEnsureHealthChecksIncorrectHealthCheck() throws Exception {
    String region = "us-west1";
    String protocol = "TCP";
    int incorrectHealthCheckPort = 9042;
    String incorrectHealthCheckName = UUID.randomUUID().toString();
    String incorrectHealthCheckUrl =
        GCPBaseUrl + "/regions/" + region + "/healthChecks/" + incorrectHealthCheckName;
    List<String> incorrectHealthCheckUrls = new ArrayList();
    incorrectHealthCheckUrls.add(incorrectHealthCheckUrl);
    HealthCheck healthCheck = new HealthCheck();
    TCPHealthCheck tcpHealthCheck = new TCPHealthCheck();
    tcpHealthCheck.setPort(incorrectHealthCheckPort);
    healthCheck.setType("TCP");
    healthCheck.setTcpHealthCheck(tcpHealthCheck);
    when(mockApiClient.getRegionalHelathCheckByName(anyString(), eq(incorrectHealthCheckName)))
        .thenReturn(healthCheck);
    String newHelathCheckName = UUID.randomUUID().toString();
    int newHealthCheckPort = 5433;
    String newHealthCheckUrl =
        GCPBaseUrl + "/regions/" + region + "/healthChecks/" + newHelathCheckName;
    List<String> healthCheckUrls = new ArrayList();
    healthCheckUrls.add(newHealthCheckUrl);
    when(mockApiClient.createNewTCPHealthCheckForPort(anyString(), anyString(), eq(5433)))
        .thenReturn(newHealthCheckUrl);
    NLBHealthCheckConfiguration healthCheckConfiguration =
        new NLBHealthCheckConfiguration(
            Arrays.asList(newHealthCheckPort), Protocol.valueOf(protocol), Arrays.asList());
    List<String> finalHealthChecks =
        gcpCloudImpl.ensureHealthChecks(
            mockApiClient, region, LB_NAME, healthCheckConfiguration, incorrectHealthCheckUrls);
    assertEquals(1, finalHealthChecks.size());
    assertEquals(newHealthCheckUrl, finalHealthChecks.get(0));
  }

  @Test
  public void testEnsureHealthChecksUpdatesNamedCheckWhenPortChanges() {
    String region = "us-west1";
    String url = GCPBaseUrl + "regions/us-west1/healthChecks/" + HC_TCP_NAME;
    HealthCheck named =
        new HealthCheck()
            .setName(HC_TCP_NAME)
            .setType("TCP")
            .setTcpHealthCheck(new TCPHealthCheck().setPort(5433));
    when(mockApiClient.getRegionalHelathCheckByName(region, HC_TCP_NAME)).thenReturn(named);
    when(mockApiClient.getRegionalHealthCheckIfExists(region, HC_TCP_NAME)).thenReturn(named);
    when(mockApiClient.updateHealthCheck(eq(region), any())).thenReturn(url);
    NLBHealthCheckConfiguration config =
        new NLBHealthCheckConfiguration(Arrays.asList(5434), Protocol.TCP, Arrays.asList());

    List<String> healthChecks =
        gcpCloudImpl.ensureHealthChecks(
            mockApiClient, region, LB_NAME, config, new ArrayList<>(List.of(url)));

    assertEquals(List.of(url), healthChecks);
    ArgumentCaptor<HealthCheck> updated = ArgumentCaptor.forClass(HealthCheck.class);
    verify(mockApiClient).updateHealthCheck(eq(region), updated.capture());
    assertEquals(Integer.valueOf(5434), updated.getValue().getTcpHealthCheck().getPort());
    verify(mockApiClient, never()).createNewTCPHealthCheckForPort(any(), any(), any());
  }

  @Test
  public void testEnsureBackendsEmpty() {
    String zone = "us-west1-a";
    String instanceName = UUID.randomUUID().toString();
    String instanceUrl = GCPBaseUrl + "/zones/" + zone + "/instances/" + instanceName;
    InstanceReference instance = new InstanceReference();
    instance.setInstance(instanceUrl);
    Map<String, List<InstanceReference>> nodeAzMap = new HashMap();
    List<InstanceReference> instances = new ArrayList();
    instances.add(instance);
    nodeAzMap.put(zone, instances);
    Backend backend = new Backend();
    List<Backend> newBackends = new ArrayList();
    newBackends.add(backend);
    Mockito.doReturn(newBackends).when(gcpCloudImpl).createNewBackends(any(), any(), any());
    Mockito.doNothing().when(gcpCloudImpl).updateInstancesInInstanceGroup(any(), any(), any());
    List<Backend> finalBackends =
        gcpCloudImpl.ensureBackends(mockApiClient, LB_NAME, nodeAzMap, null);
    assertEquals(1, finalBackends.size());
    assertEquals(backend, finalBackends.get(0));
  }

  @Test
  public void testEnsureBackendsIncorrectBackends() throws Exception {
    Backend initialBackend = new Backend();
    String initialInstanceGroupUrl = GCPBaseUrl + "/zones/us-east1-a/instancegroups/ig-test";
    initialBackend.setGroup(initialInstanceGroupUrl);
    List<Backend> initialBackends = new ArrayList();
    initialBackends.add(initialBackend);
    String zone = "us-west1-a";
    String instanceName = UUID.randomUUID().toString();
    String instanceUrl = GCPBaseUrl + "/zones/" + zone + "/instances/" + instanceName;
    InstanceReference instance = new InstanceReference();
    instance.setInstance(instanceUrl);
    Map<String, List<InstanceReference>> nodeAzMap = new HashMap();
    List<InstanceReference> instances = new ArrayList();
    instances.add(instance);
    nodeAzMap.put(zone, instances);
    Backend backend = new Backend();
    List<Backend> newBackends = new ArrayList();
    newBackends.add(backend);
    Mockito.doReturn(newBackends).when(gcpCloudImpl).createNewBackends(any(), any(), any());
    Mockito.doNothing().when(gcpCloudImpl).updateInstancesInInstanceGroup(any(), any(), any());
    List<Backend> finalBackends =
        gcpCloudImpl.ensureBackends(mockApiClient, LB_NAME, nodeAzMap, initialBackends);
    assertEquals(1, finalBackends.size());
    assertEquals(backend, finalBackends.get(0));
  }

  @Test
  public void testManageNodeGroupBasic() throws Exception {
    String region = "us-west1";
    String lbName = "lb-test";
    NodeID node1 = new NodeID(UUID.randomUUID().toString(), UUID.randomUUID().toString());
    NodeID node2 = new NodeID(UUID.randomUUID().toString(), UUID.randomUUID().toString());
    AvailabilityZone az1 = new AvailabilityZone();
    az1.setName("us-west1-a");
    AvailabilityZone az2 = new AvailabilityZone();
    az2.setName("us-west1-b");
    Map<AvailabilityZone, Set<NodeID>> azToNodeIdMap = new HashMap();
    azToNodeIdMap.put(az1, Set.of(node1));
    azToNodeIdMap.put(az2, Set.of(node2));
    InstanceReference instance1 = new InstanceReference();
    instance1.setInstance(GCPBaseUrl + "zones/" + az1.getName() + "/instances/" + node1.getName());
    InstanceReference instance2 = new InstanceReference();
    instance2.setInstance(GCPBaseUrl + "zones/" + az2.getName() + "/instances/" + node2.getName());
    when(mockApiClient.getInstancesInZoneByNames(eq(az1.getName()), any()))
        .thenReturn(Arrays.asList(instance1));
    when(mockApiClient.getInstancesInZoneByNames(eq(az2.getName()), any()))
        .thenReturn(Arrays.asList(instance2));
    BackendService backendService = new BackendService();
    backendService.setProtocol("TCP");
    Backend backend = backend("us-west1-a", "ig-test");
    HealthCheck healthCheck = new HealthCheck();
    String healthCheckUrl =
        GCPBaseUrl + "regions/" + region + "/healthchecks/" + UUID.randomUUID().toString();
    backendService.setBackends(Arrays.asList(backend));
    backendService.setHealthChecks(Arrays.asList(healthCheckUrl));
    when(mockApiClient.getBackendService(any(), any())).thenReturn(backendService);
    Mockito.doReturn(Arrays.asList(backend))
        .when(gcpCloudImpl)
        .ensureBackends(any(), any(), any(), any());
    Mockito.doReturn(Arrays.asList())
        .when(gcpCloudImpl)
        .ensureHealthChecks(any(), any(), any(), any(), any());
    Mockito.doNothing().when(mockApiClient).updateBackendService(any(), any());
    when(mockApiClient.getRegionalForwardingRulesForBackend(any(), any())).thenReturn(null);
    Mockito.doNothing().when(gcpCloudImpl).ensureForwardingRules(any(), any(), any());
    List<Integer> ports = new ArrayList();
    ports.add(5433);
    ports.add(9042);
    NLBHealthCheckConfiguration healthCheckConfiguration =
        new NLBHealthCheckConfiguration(ports, Protocol.TCP, Arrays.asList());
    gcpCloudImpl.manageNodeGroup(
        defaultProvider,
        region,
        lbName,
        azToNodeIdMap,
        "TCP",
        ports,
        healthCheckConfiguration,
        mockApiClient);
  }

  // A load balancer that the user created, with the names that YBA gave before the hashed names:
  // hc-<port><UUID> and ig-<UUID>.
  private static final String USER_LB_NAME = "customer-lb";
  private static final String OLD_HC_NAME = "hc-54331b4e28ba-2fa1-11d2-883f-0016d3cca427";
  private static final String OLD_GROUP_A = "ig-6ba7b810-9dad-11d1-80b4-00c04fd430c8";
  private static final String OLD_GROUP_B = "ig-6ba7b811-9dad-11d1-80b4-00c04fd430c8";

  private String healthCheckUrl(String name) {
    return GCPBaseUrl + "regions/us-west1/healthChecks/" + name;
  }

  private Backend backend(String zone, String group) {
    return new Backend().setGroup(GCPBaseUrl + "zones/" + zone + "/instanceGroups/" + group);
  }

  // The backend service uses a TCP check on 5433. The universe now has one node, n1 in us-west1-a.
  private void givenUserLb(String healthCheckName, Backend... backends) throws Exception {
    when(mockApiClient.getBackendService("us-west1", USER_LB_NAME))
        .thenReturn(
            new BackendService()
                .setBackends(new ArrayList<>(List.of(backends)))
                .setHealthChecks(new ArrayList<>(List.of(healthCheckUrl(healthCheckName)))));
    when(mockApiClient.getRegionalHelathCheckByName("us-west1", healthCheckName))
        .thenReturn(
            new HealthCheck().setType("TCP").setTcpHealthCheck(new TCPHealthCheck().setPort(5433)));
    when(mockApiClient.getInstancesInZoneByNames(eq("us-west1-a"), any()))
        .thenReturn(List.of(instanceRef("us-west1-a", "n1")));
    when(mockApiClient.getInstanceGroup(eq("us-west1-a"), any())).thenReturn(new InstanceGroup());
    when(mockApiClient.getRegionalForwardingRulesForBackend(any(), any()))
        .thenReturn(
            List.of(new ForwardingRule().setIPProtocol("TCP").setPorts(List.of("5433", "5434"))));
  }

  private void manageUserLb(int port) {
    AvailabilityZone zoneA = new AvailabilityZone();
    zoneA.setName("us-west1-a");
    gcpCloudImpl.manageNodeGroup(
        defaultProvider,
        "us-west1",
        USER_LB_NAME,
        Map.of(zoneA, Set.of(new NodeID("n1", UUID.randomUUID().toString()))),
        "TCP",
        new ArrayList<>(List.of(port)),
        new NLBHealthCheckConfiguration(List.of(port), Protocol.TCP, List.of()),
        mockApiClient);
  }

  @Test
  public void testManageNodeGroupKeepsUsedResourcesWithPreHashNames() throws Exception {
    Backend backendA = backend("us-west1-a", OLD_GROUP_A);
    givenUserLb(OLD_HC_NAME, backendA);
    InstanceReference removed = instanceRef("us-west1-a", "n0");
    when(mockApiClient.getInstancesForInstanceGroup("us-west1-a", OLD_GROUP_A))
        .thenReturn(List.of(removed));

    manageUserLb(5433);

    verify(mockApiClient)
        .addInstancesToInstaceGroup(
            "us-west1-a", OLD_GROUP_A, List.of(instanceRef("us-west1-a", "n1")));
    verify(mockApiClient)
        .removeInstancesFromInstaceGroup("us-west1-a", OLD_GROUP_A, List.of(removed));
    verify(mockApiClient, never()).createNewInstanceGroupInZone(any(), any());
    verify(mockApiClient, never()).createNewTCPHealthCheckForPort(any(), any(), any());
    verify(mockApiClient, never()).updateHealthCheck(any(), any());
    verify(mockApiClient, never()).deleteInstanceGroup(any(), any());
    verify(mockApiClient, never()).deleteRegionalHealthCheck(any(), any());
    ArgumentCaptor<BackendService> updated = ArgumentCaptor.forClass(BackendService.class);
    verify(mockApiClient).updateBackendService(eq("us-west1"), updated.capture());
    assertEquals(List.of(backendA), updated.getValue().getBackends());
    assertEquals(List.of(healthCheckUrl(OLD_HC_NAME)), updated.getValue().getHealthChecks());
  }

  @Test
  public void testManageNodeGroupDeletesUnusedGroupAndCheckAfterUpdatingTheBackendService()
      throws Exception {
    // Zone b lost its last node, and the port changed.
    givenUserLb(
        OLD_HC_NAME, backend("us-west1-a", OLD_GROUP_A), backend("us-west1-b", OLD_GROUP_B));
    String newHcName = GCPCloudImpl.getHealthCheckName(USER_LB_NAME, Protocol.TCP);
    when(mockApiClient.createNewTCPHealthCheckForPort("us-west1", newHcName, 5434))
        .thenReturn(healthCheckUrl(newHcName));
    // A failed delete stops neither the next delete nor the task.
    Mockito.doThrow(new PlatformServiceException(BAD_REQUEST, "in use"))
        .when(mockApiClient)
        .deleteInstanceGroup("us-west1-b", OLD_GROUP_B);

    manageUserLb(5434);

    // GCP refuses to delete a group or check that the backend service uses.
    InOrder groups = Mockito.inOrder(mockApiClient);
    groups.verify(mockApiClient).updateBackendService(eq("us-west1"), any());
    groups.verify(mockApiClient).deleteInstanceGroup("us-west1-b", OLD_GROUP_B);
    InOrder checks = Mockito.inOrder(mockApiClient);
    checks.verify(mockApiClient).updateBackendService(eq("us-west1"), any());
    checks.verify(mockApiClient).deleteRegionalHealthCheck("us-west1", OLD_HC_NAME);
    verify(mockApiClient, never()).deleteInstanceGroup("us-west1-a", OLD_GROUP_A);
  }

  @Test
  public void testManageNodeGroupKeepsUnusedGroupAndCheckThatYbaDidNotName() throws Exception {
    givenUserLb(
        "customer-hc", backend("us-west1-a", OLD_GROUP_A), backend("us-west1-b", "customer-ig"));
    when(mockApiClient.createNewTCPHealthCheckForPort(any(), any(), eq(5434)))
        .thenReturn(healthCheckUrl("hc-new"));

    manageUserLb(5434);

    verify(mockApiClient).updateBackendService(eq("us-west1"), any());
    verify(mockApiClient, never()).deleteInstanceGroup(any(), any());
    verify(mockApiClient, never()).deleteRegionalHealthCheck(any(), any());
  }

  private static final String LB_NAME = "lbi-h6uf6zcxc5cwfm74fsld6zvpuy";
  // SHA-256 of LB_NAME starts with 588767b387c1db466530c8.
  private static final String HC_TCP_NAME = "hc-588767b387c1db466530c8-tcp";
  private static final String HC_HTTP_NAME = "hc-588767b387c1db466530c8-http";
  private static final String IG_NAME = "ig-588767b387c1db466530c8";
  private static final String LB_REGION = "us-west1";
  // YBA builds network and subnet URLs on the www.googleapis.com host.
  private static final String GCP_API_BASE =
      "https://www.googleapis.com/compute/v1/projects/project/";
  private static final String NETWORK_URL = GCP_API_BASE + "global/networks/yb-network";
  private static final String SUBNET_URL =
      GCP_API_BASE + "regions/us-west1/subnetworks/subnet-us-west1";
  private final String HC_URL = GCPBaseUrl + "regions/us-west1/healthChecks/" + HC_TCP_NAME;
  private final String BS_URL = GCPBaseUrl + "regions/us-west1/backendServices/" + LB_NAME;
  private final String ADDRESS_URL = GCPBaseUrl + "regions/us-west1/addresses/" + LB_NAME;
  private static final List<Integer> LB_PORTS = List.of(5433, 9042);
  private List<AvailabilityZone> lbZones;

  private Region setupLbProvider() {
    Region region = Region.create(defaultProvider, LB_REGION, LB_REGION, "yb-image");
    lbZones =
        List.of(
            AvailabilityZone.createOrThrow(region, "us-west1-a", "us-west1-a", "subnet-us-west1"),
            AvailabilityZone.createOrThrow(region, "us-west1-b", "us-west1-b", "subnet-us-west1"));
    ((GCPCloudInfo) CloudInfoInterface.get(defaultProvider)).setDestVpcId("yb-network");
    Mockito.doReturn(mockApiClient).when(gcpCloudImpl).getApiClient(any());
    return region;
  }

  private Address lbAddress() {
    return new Address()
        .setName(LB_NAME)
        .setAddress("10.150.0.5")
        .setSelfLink(ADDRESS_URL)
        .setLabelFingerprint("fp-a");
  }

  private static ForwardingRule lbForwardingRule(List<String> ports, String labelFingerprint) {
    return new ForwardingRule()
        .setName(LB_NAME)
        .setPorts(ports)
        .setLabelFingerprint(labelFingerprint);
  }

  private void givenBackendServiceAndAddress() {
    when(mockApiClient.getRegionalBackendServiceIfExists(LB_REGION, LB_NAME))
        .thenReturn(new BackendService().setSelfLink(BS_URL));
    when(mockApiClient.getRegionalAddressIfExists(LB_REGION, LB_NAME)).thenReturn(lbAddress());
  }

  private String ensureLb(List<AvailabilityZone> zones, Map<String, String> tags) {
    return gcpCloudImpl.ensureManagedLoadBalancer(
        defaultProvider, LB_REGION, LB_NAME, zones, LB_PORTS, tags);
  }

  private void verifyNoLbResourceCreated() {
    verify(mockApiClient, never()).createNewTCPHealthCheckForPort(any(), any(), any());
    verify(mockApiClient, never()).createInternalBackendService(any(), any(), any(), any());
    verify(mockApiClient, never()).createInternalAddress(any(), any(), any());
    verify(mockApiClient, never())
        .createInternalForwardingRule(any(), any(), any(), any(), any(), any(), any());
  }

  @Test
  public void testEnsureLbCreatesTheFourResourcesByNameAndReturnsTheAddress() {
    setupLbProvider();
    when(mockApiClient.createNewTCPHealthCheckForPort(LB_REGION, HC_TCP_NAME, 5433))
        .thenReturn(HC_URL);
    when(mockApiClient.createInternalBackendService(LB_REGION, LB_NAME, NETWORK_URL, HC_URL))
        .thenReturn(BS_URL);
    when(mockApiClient.createInternalAddress(LB_REGION, LB_NAME, SUBNET_URL))
        .thenReturn(lbAddress());
    when(mockApiClient.createInternalForwardingRule(
            LB_REGION, LB_NAME, BS_URL, ADDRESS_URL, NETWORK_URL, SUBNET_URL, LB_PORTS))
        .thenReturn(lbForwardingRule(List.of("5433", "9042"), "fp-f"));

    String address = ensureLb(lbZones, Map.of("universe-name", "U1"));

    assertEquals("10.150.0.5", address);
    // The health check is on the first forwarded port, and ensureHealthChecks finds it by name.
    verify(mockApiClient).createNewTCPHealthCheckForPort(LB_REGION, HC_TCP_NAME, 5433);
    verify(mockApiClient).createInternalBackendService(LB_REGION, LB_NAME, NETWORK_URL, HC_URL);
    verify(mockApiClient).createInternalAddress(LB_REGION, LB_NAME, SUBNET_URL);
    // One rule on every port, or ensureForwardingRules fails.
    verify(mockApiClient)
        .createInternalForwardingRule(
            LB_REGION, LB_NAME, BS_URL, ADDRESS_URL, NETWORK_URL, SUBNET_URL, LB_PORTS);
    verify(mockApiClient)
        .setAddressLabels(LB_REGION, LB_NAME, "fp-a", Map.of("universe-name", "u1"));
    verify(mockApiClient)
        .setForwardingRuleLabels(LB_REGION, LB_NAME, "fp-f", Map.of("universe-name", "u1"));
  }

  @Test
  public void testEnsureLbReusesTheResourcesAndKeepsTheAddress() {
    setupLbProvider();
    givenBackendServiceAndAddress();
    when(mockApiClient.getRegionalForwardingRuleIfExists(LB_REGION, LB_NAME))
        .thenReturn(lbForwardingRule(List.of("5433", "9042"), "fp-f"));

    String address = ensureLb(lbZones, Map.of());

    assertEquals("10.150.0.5", address);
    verifyNoLbResourceCreated();
    verify(mockApiClient, never()).deleteRegionalForwardingRule(any(), any());
  }

  @Test
  public void testEnsureLbReplacesForwardingRuleThatMissesPortAndKeepsItsLabels() {
    setupLbProvider();
    givenBackendServiceAndAddress();
    // YCQL was enabled after the rule was created.
    when(mockApiClient.getRegionalForwardingRuleIfExists(LB_REGION, LB_NAME))
        .thenReturn(lbForwardingRule(List.of("5433"), "fp-old").setLabels(Map.of("owner", "dba")));
    when(mockApiClient.createInternalForwardingRule(
            LB_REGION, LB_NAME, BS_URL, ADDRESS_URL, NETWORK_URL, SUBNET_URL, LB_PORTS))
        .thenReturn(lbForwardingRule(List.of("5433", "9042"), "fp-new"));

    ensureLb(lbZones, Map.of("universe-name", "u1"));

    InOrder inOrder = Mockito.inOrder(mockApiClient);
    inOrder.verify(mockApiClient).deleteRegionalForwardingRule(LB_REGION, LB_NAME);
    inOrder
        .verify(mockApiClient)
        .createInternalForwardingRule(
            LB_REGION, LB_NAME, BS_URL, ADDRESS_URL, NETWORK_URL, SUBNET_URL, LB_PORTS);
    // The new rule's fingerprint, with the labels of the old rule.
    verify(mockApiClient)
        .setForwardingRuleLabels(
            LB_REGION, LB_NAME, "fp-new", Map.of("owner", "dba", "universe-name", "u1"));
    verify(mockApiClient, never()).deleteRegionalAddress(any(), any());
    verify(mockApiClient, never()).createInternalAddress(any(), any(), any());
  }

  @Test
  public void testEnsureLbRecreatesMissingForwardingRuleOnExistingAddress() {
    setupLbProvider();
    // A replacement deleted the rule, and its create failed.
    givenBackendServiceAndAddress();
    when(mockApiClient.createInternalForwardingRule(
            LB_REGION, LB_NAME, BS_URL, ADDRESS_URL, NETWORK_URL, SUBNET_URL, LB_PORTS))
        .thenReturn(lbForwardingRule(List.of("5433", "9042"), "fp-f"));

    String address = ensureLb(lbZones, Map.of());

    assertEquals("10.150.0.5", address);
    verify(mockApiClient)
        .createInternalForwardingRule(
            LB_REGION, LB_NAME, BS_URL, ADDRESS_URL, NETWORK_URL, SUBNET_URL, LB_PORTS);
    verify(mockApiClient, never()).createInternalAddress(any(), any(), any());
  }

  @Test
  public void testEnsureLbFailsBeforeCreatingAnythingWhenNoZoneHasSubnet() {
    setupLbProvider();
    AvailabilityZone zoneWithoutSubnet = new AvailabilityZone();
    zoneWithoutSubnet.setCode("us-west1-c");

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class, () -> ensureLb(List.of(zoneWithoutSubnet), Map.of()));

    assertEquals(BAD_REQUEST, e.getHttpStatus());
    verifyNoLbResourceCreated();
  }

  @Test
  public void testDeleteLbDeletesReferencingResourcesBeforeTheOnesTheyReference() {
    setupLbProvider();
    String randomHcUrl = GCPBaseUrl + "regions/us-west1/healthChecks/hc-5433-random";
    String randomGroupUrl = GCPBaseUrl + "zones/us-west1-b/instanceGroups/ig-random";
    when(mockApiClient.getRegionalBackendServiceIfExists(LB_REGION, LB_NAME))
        .thenReturn(
            new BackendService()
                .setHealthChecks(List.of(randomHcUrl))
                .setBackends(List.of(new Backend().setGroup(randomGroupUrl))));

    gcpCloudImpl.deleteManagedLoadBalancer(defaultProvider, LB_REGION, LB_NAME);

    // GCP refuses to delete a resource that another still references.
    InOrder checks = Mockito.inOrder(mockApiClient);
    checks.verify(mockApiClient).deleteRegionalForwardingRule(LB_REGION, LB_NAME);
    checks.verify(mockApiClient).deleteRegionalBackendService(LB_REGION, LB_NAME);
    checks.verify(mockApiClient).deleteRegionalHealthCheck(LB_REGION, "hc-5433-random");
    InOrder groups = Mockito.inOrder(mockApiClient);
    groups.verify(mockApiClient).deleteRegionalBackendService(LB_REGION, LB_NAME);
    groups.verify(mockApiClient).deleteInstanceGroup("us-west1-b", "ig-random");
    InOrder address = Mockito.inOrder(mockApiClient);
    address.verify(mockApiClient).deleteRegionalForwardingRule(LB_REGION, LB_NAME);
    address.verify(mockApiClient).deleteRegionalAddress(LB_REGION, LB_NAME);
  }

  @Test
  public void testDeleteLbWithoutBackendServiceDeletesNamedChecksAndGroupsInEveryZone() {
    Region region = setupLbProvider();
    AvailabilityZone inactiveZone =
        AvailabilityZone.createOrThrow(region, "us-west1-c", "us-west1-c", "subnet-us-west1");
    inactiveZone.setActive(false);
    inactiveZone.save();

    gcpCloudImpl.deleteManagedLoadBalancer(defaultProvider, LB_REGION, LB_NAME);

    verify(mockApiClient).deleteRegionalForwardingRule(LB_REGION, LB_NAME);
    verify(mockApiClient, never()).deleteRegionalBackendService(any(), any());
    verify(mockApiClient).deleteRegionalHealthCheck(LB_REGION, HC_TCP_NAME);
    verify(mockApiClient).deleteRegionalHealthCheck(LB_REGION, HC_HTTP_NAME);
    verify(mockApiClient).deleteInstanceGroup("us-west1-a", IG_NAME);
    verify(mockApiClient).deleteInstanceGroup("us-west1-b", IG_NAME);
    verify(mockApiClient).deleteInstanceGroup("us-west1-c", IG_NAME);
    verify(mockApiClient).deleteRegionalAddress(LB_REGION, LB_NAME);
  }

  @Test
  public void testToLabelsLowercasesReplacesInvalidCharactersAndSkipsBadKeys() {
    Map<String, String> tags = new HashMap<>();
    tags.put("Universe-Name", "My U1.prod");
    tags.put("owner", null);
    tags.put("1st", "x");
    tags.put("_team", "x");

    assertEquals(Map.of("universe-name", "my_u1_prod", "owner", ""), GCPCloudImpl.toLabels(tags));
  }

  @Test
  public void testToLabelsCutsKeysAndValuesToSixtyThreeCharacters() {
    String key64 = "k" + "a".repeat(63);
    String value63 = "v" + "a".repeat(62);

    assertEquals(
        Map.of("k" + "a".repeat(62), value63), GCPCloudImpl.toLabels(Map.of(key64, value63)));
  }
}
