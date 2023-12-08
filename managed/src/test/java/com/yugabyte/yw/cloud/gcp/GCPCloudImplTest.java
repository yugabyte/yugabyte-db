package com.yugabyte.yw.cloud.gcp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.google.api.services.compute.model.Backend;
import com.google.api.services.compute.model.BackendService;
import com.google.api.services.compute.model.ForwardingRule;
import com.google.api.services.compute.model.HealthCheck;
import com.google.api.services.compute.model.InstanceReference;
import com.google.api.services.compute.model.TCPHealthCheck;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
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
import org.mockito.Mock;
import org.mockito.Mockito;

public class GCPCloudImplTest extends FakeDBApplication {

  private GCPCloudImpl gcpCloudImpl;
  private Customer customer;
  private Provider defaultProvider;
  private Region defaultRegion;
  @Mock private GCPProjectApiClient mockApiClient;
  private String GCPBaseUrl = "https://compute.googleapis.com/compute/v1/projects/project/";

  @Before
  public void setup() throws Exception {
    gcpCloudImpl = spy(new GCPCloudImpl());
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
    String instanceGroupName = "ig-" + UUID.randomUUID().toString();
    String instanceGroupUrl = GCPBaseUrl + "zones/us-west1-a/instanceGroups/" + instanceGroupName;
    String instanceName = UUID.randomUUID().toString();
    String instanceUrl = GCPBaseUrl + "zones/us-west1-a/instances/" + instanceName;
    InstanceReference instance = new InstanceReference();
    instance.setInstance(instanceUrl);
    List<InstanceReference> instances = Arrays.asList(instance);
    Map<String, List<InstanceReference>> nodeAzMap = new HashMap();
    nodeAzMap.put("us-west1-a", instances);
    Mockito.doNothing()
        .when(mockApiClient)
        .addInstancesToInstaceGroup(eq("us-west1-a"), eq(instanceGroupName), eq(instances));
    when(mockApiClient.createNewInstanceGroupInZone("us-west1-a")).thenReturn(instanceGroupUrl);
    List<Backend> newBakcends = gcpCloudImpl.createNewBackends(mockApiClient, nodeAzMap);
    // Assert that exactly 1 new backend was created
    assertEquals(newBakcends.size(), 1);
    // Assert that the backend created has the correct name
    assertEquals(newBakcends.get(0).getGroup(), instanceGroupUrl);
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
    when(mockApiClient.createNewTCPHealthCheckForPort(anyString(), eq(5433)))
        .thenReturn(healthCheckUrl);
    List<String> finalHealthChecks =
        gcpCloudImpl.ensureHealthChecks(
            mockApiClient, region, healthCheckConfiguration, new ArrayList<String>());
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
    when(mockApiClient.createNewTCPHealthCheckForPort(anyString(), eq(5433)))
        .thenReturn(healthCheckUrl);
    List<String> finalHealthChecks =
        gcpCloudImpl.ensureHealthChecks(mockApiClient, region, healthCheckConfiguration, null);
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
    when(mockApiClient.createNewTCPHealthCheckForPort(anyString(), eq(5433)))
        .thenReturn(newHealthCheckUrl);
    NLBHealthCheckConfiguration healthCheckConfiguration =
        new NLBHealthCheckConfiguration(
            Arrays.asList(newHealthCheckPort), Protocol.valueOf(protocol), Arrays.asList());
    List<String> finalHealthChecks =
        gcpCloudImpl.ensureHealthChecks(
            mockApiClient, region, healthCheckConfiguration, incorrectHealthCheckUrls);
    assertEquals(1, finalHealthChecks.size());
    assertEquals(newHealthCheckUrl, finalHealthChecks.get(0));
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
    Mockito.doReturn(newBackends).when(gcpCloudImpl).createNewBackends(any(), any());
    Mockito.doNothing().when(gcpCloudImpl).updateInstancesInInstanceGroup(any(), any(), any());
    List<Backend> finalBackends = gcpCloudImpl.ensureBackends(mockApiClient, nodeAzMap, null);
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
    Mockito.doNothing().when(mockApiClient).deleteBackends(any());
    Mockito.doReturn(newBackends).when(gcpCloudImpl).createNewBackends(any(), any());
    Mockito.doNothing().when(gcpCloudImpl).updateInstancesInInstanceGroup(any(), any(), any());
    List<Backend> finalBackends =
        gcpCloudImpl.ensureBackends(mockApiClient, nodeAzMap, initialBackends);
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
    Backend backend = new Backend();
    HealthCheck healthCheck = new HealthCheck();
    String healthCheckUrl =
        GCPBaseUrl + "regions/" + region + "/healthchecks/" + UUID.randomUUID().toString();
    backendService.setBackends(Arrays.asList(backend));
    backendService.setHealthChecks(Arrays.asList(healthCheckUrl));
    when(mockApiClient.getBackendService(any(), any())).thenReturn(backendService);
    Mockito.doReturn(Arrays.asList(backend)).when(gcpCloudImpl).ensureBackends(any(), any(), any());
    Mockito.doReturn(Arrays.asList())
        .when(gcpCloudImpl)
        .ensureHealthChecks(any(), any(), any(), any());
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
}
