package com.yugabyte.yw.cloud;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.io.IOException;
import java.util.HashSet;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;
import play.api.Play;
import play.libs.Json;
import software.amazon.awssdk.services.ec2.Ec2Client;
import software.amazon.awssdk.services.elasticloadbalancingv2.ElasticLoadBalancingV2Client;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeLoadBalancersRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeLoadBalancersResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancer;

@Slf4j
public class CloudAPITest extends FakeDBApplication {
  private CloudAPI.Factory cloudAPIFactory;
  private CloudAPI cloudAPI;
  private String lbName;
  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone az;

  @Mock private Ec2Client mockEC2Client;

  @Mock private ElasticLoadBalancingV2Client mockELBClient;

  private AutoCloseable openedMocks;

  private AWSCloudImpl awsCloudImpl;

  private static final String mockJsonPath =
      "com/yugabyte/yw/controllers/mock_manage_load_balancer_test.json";
  private static String mockJsonString;

  @Before
  public void setup() throws IOException {
    openedMocks = MockitoAnnotations.openMocks(this);

    lbName = "yb-spu-nlb";
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    region = Region.create(provider, "us-west-2", "US West 2", "yb-image-1");

    awsCloudImpl = spy(new AWSCloudImpl(null));
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), any());
    Mockito.doReturn(mockELBClient).when(awsCloudImpl).getELBClient(any(), any());
    mockJsonString =
        IOUtils.toString(Play.class.getClassLoader().getResourceAsStream(mockJsonPath), "UTF-8");
    JsonNode mockJson = Json.parse(mockJsonString);
    JsonNode mockLBJson = mockJson.get("LoadBalancers").get(0);
    LoadBalancer lb =
        LoadBalancer.builder().loadBalancerArn(mockLBJson.get("LoadBalancerArn").asText()).build();
    DescribeLoadBalancersResponse mockResult =
        DescribeLoadBalancersResponse.builder().loadBalancers(lb).build();
    when(mockELBClient.describeLoadBalancers(any(DescribeLoadBalancersRequest.class)))
        .thenReturn(mockResult);
  }

  @Test
  public void testCreateNodeGroup() throws Exception {
    Set<String> nodes = new HashSet<>();
    nodes.add("yb-spu1");
    nodes.add("yb-spu2");
    nodes.add("yb-spu3");
    LoadBalancer lb = awsCloudImpl.getLoadBalancerByName(provider, region.getCode(), "yb-spu-nlb");
    String lbArn =
        "arn:aws:elasticloadbalancing:"
            + "us-west-2:454529406029:loadbalancer/net/yb-spu-nlb/77d043677679338a";
    if (lb == null) log.debug("AWSTEST: LB NULL");
    assertEquals(lb.loadBalancerArn(), lbArn);
  }

  @After
  public void teardown() throws Exception {
    openedMocks.close();
  }
}
