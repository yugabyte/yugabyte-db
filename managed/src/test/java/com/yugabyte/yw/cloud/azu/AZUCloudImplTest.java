package com.yugabyte.yw.cloud.azu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;

import com.azure.core.management.SubResource;
import com.azure.resourcemanager.network.fluent.models.BackendAddressPoolInner;
import com.azure.resourcemanager.network.fluent.models.FrontendIpConfigurationInner;
import com.azure.resourcemanager.network.fluent.models.LoadBalancingRuleInner;
import com.azure.resourcemanager.network.fluent.models.ProbeInner;
import com.azure.resourcemanager.network.models.ProbeProtocol;
import com.azure.resourcemanager.network.models.TransportProtocol;
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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;

public class AZUCloudImplTest extends FakeDBApplication {

  private AZUCloudImpl azuCloudImpl;
  private Customer customer;
  private Provider defaultProvider;
  private Region defaultRegion;
  @Mock private AZUResourceGroupApiClient mockApiClient;

  @Before
  public void setup() throws Exception {
    azuCloudImpl = spy(new AZUCloudImpl());
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
    providerDetails.setCloudInfo(cloudInfo);
    defaultProvider.setDetails(providerDetails);
    mockApiClient = mock(AZUResourceGroupApiClient.class);
  }

  @Test
  public void testCreateNewLbRuleSuccess() {
    List<SubResource> backendSubResources = Arrays.asList((new SubResource()).withId("backend1"));
    SubResource frontendIpConfigSubResource = (new SubResource()).withId("frontendIP");
    Integer port = 5433;
    LoadBalancingRuleInner lbRule =
        azuCloudImpl.createNewLoadBalancingRuleForPort(
            "TCP", port, backendSubResources, frontendIpConfigSubResource);
    assertEquals(port, lbRule.frontendPort());
    assertEquals(port, lbRule.backendPort());
    assertEquals(TransportProtocol.TCP, lbRule.protocol());
    assertEquals(backendSubResources, lbRule.backendAddressPools());
    assertEquals(frontendIpConfigSubResource, lbRule.frontendIpConfiguration());
  }

  @Test
  public void testCreateNewLbRuleFail() {
    List<SubResource> backendSubResources = Arrays.asList((new SubResource()).withId("backend1"));
    SubResource frontendIpConfigSubResource = (new SubResource()).withId("frontendIP");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                azuCloudImpl.createNewLoadBalancingRuleForPort(
                    "HTTP", 5433, backendSubResources, frontendIpConfigSubResource));
    assert (exception.getMessage().contains("Only TCP protocol is supported"));
  }

  @Test
  public void testCreateNewProbeSuccess() {
    Integer port = 5433;
    ProbeInner probe = azuCloudImpl.createNewTCPProbeForPort(port);
    assertEquals(ProbeProtocol.TCP, probe.protocol());
    assertEquals(port, probe.port());
  }

  @Test
  public void testEnsureLbRules() {
    List<Integer> portsToCheck = Arrays.asList(5433);
    BackendAddressPoolInner backendAddressPool = new BackendAddressPoolInner();
    backendAddressPool = backendAddressPool.withId("Backend1");
    List<BackendAddressPoolInner> backends = Arrays.asList(backendAddressPool);
    FrontendIpConfigurationInner frontend = new FrontendIpConfigurationInner();
    frontend = frontend.withId("Fronted");
    List<LoadBalancingRuleInner> newLbRules =
        azuCloudImpl.ensureLoadBalancingRules("TCP", portsToCheck, null, backends, frontend);
    assertEquals(1, newLbRules.size());
  }

  @Test
  public void testEnsureProbes() {
    List<Integer> ports = Arrays.asList(5433);
    NLBHealthCheckConfiguration healthCheckConfiguration =
        new NLBHealthCheckConfiguration(ports, Protocol.TCP, new ArrayList<>());
    List<ProbeInner> newProbes =
        azuCloudImpl.ensureProbesForPorts(healthCheckConfiguration, new ArrayList<ProbeInner>());
    assertEquals(1, newProbes.size());
  }
}
