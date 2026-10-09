package com.yugabyte.yw.cloud.aws;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.CertificateHelperTest;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.InOrder;
import org.mockito.Mock;
import org.mockito.Mockito;
import software.amazon.awssdk.awscore.exception.AwsErrorDetails;
import software.amazon.awssdk.awscore.exception.AwsServiceException;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.ec2.Ec2Client;
import software.amazon.awssdk.services.ec2.model.AuthorizeSecurityGroupIngressRequest;
import software.amazon.awssdk.services.ec2.model.CreateKeyPairRequest;
import software.amazon.awssdk.services.ec2.model.CreateSecurityGroupRequest;
import software.amazon.awssdk.services.ec2.model.DescribeImagesRequest;
import software.amazon.awssdk.services.ec2.model.DescribeImagesResponse;
import software.amazon.awssdk.services.ec2.model.DescribeInstanceTypesRequest;
import software.amazon.awssdk.services.ec2.model.DescribeInstancesRequest;
import software.amazon.awssdk.services.ec2.model.DescribeKeyPairsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeSecurityGroupsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeSecurityGroupsResponse;
import software.amazon.awssdk.services.ec2.model.DescribeSubnetsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeSubnetsResponse;
import software.amazon.awssdk.services.ec2.model.DescribeVpcsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeVpcsResponse;
import software.amazon.awssdk.services.ec2.model.Ec2Exception;
import software.amazon.awssdk.services.ec2.model.Image;
import software.amazon.awssdk.services.ec2.model.SecurityGroup;
import software.amazon.awssdk.services.ec2.model.Subnet;
import software.amazon.awssdk.services.ec2.model.Vpc;
import software.amazon.awssdk.services.elasticloadbalancingv2.ElasticLoadBalancingV2Client;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.Action;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ActionTypeEnum;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateListenerRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateListenerResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateLoadBalancerRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateLoadBalancerResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateTargetGroupRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateTargetGroupResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DeleteLoadBalancerRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DeleteTargetGroupRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeListenersRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeListenersResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeLoadBalancersRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeLoadBalancersResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeTargetGroupsRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeTargetGroupsResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DuplicateLoadBalancerNameException;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DuplicateTargetGroupNameException;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ForwardActionConfig;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.Listener;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancer;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancerNotFoundException;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancerSchemeEnum;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancerState;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancerStateEnum;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancerTypeEnum;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ModifyLoadBalancerAttributesRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ResourceInUseException;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.SetSubnetsRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetGroup;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetGroupTuple;
import software.amazon.awssdk.services.route53.Route53Client;
import software.amazon.awssdk.services.route53.model.GetHostedZoneRequest;
import software.amazon.awssdk.services.route53.model.GetHostedZoneResponse;
import software.amazon.awssdk.services.sts.StsClient;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityRequest;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityResponse;

public class AWSCloudImplTest extends FakeDBApplication {

  private AWSCloudImpl awsCloudImpl;
  private Customer customer;
  private Provider defaultProvider;
  private NLBHealthCheckConfiguration defaultNlbHealthCheckConfiguration;
  private Region defaultRegion;
  @Mock private Ec2Client mockEC2Client;
  @Mock private Route53Client mockRoute53Client;
  @Mock private StsClient mockSTSService;
  @Mock private ElasticLoadBalancingV2Client mockElbClient;

  private String AMAZON_COMMON_ERROR_MSG =
      "(Service: null; Status Code: 0;" + " Error Code: null; Request ID: null; Proxy: null)";

  @Before
  public void setup() {
    awsCloudImpl = spy(new AWSCloudImpl(null, null));
    mockEC2Client = mock(Ec2Client.class);
    mockElbClient = mock(ElasticLoadBalancingV2Client.class);
    mockRoute53Client = mock(Route53Client.class);
    mockSTSService = mock(StsClient.class);
    customer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(customer);
    defaultRegion = new Region();
    defaultRegion.setProvider(defaultProvider);
    defaultRegion.setCode("us-west-2");
    defaultRegion.setName("us-west-2");
    AvailabilityZone az = new AvailabilityZone();
    az.setCode("subnet-1");
    defaultRegion.setZones(Arrays.asList(az));
    defaultProvider.getRegions().add(defaultRegion);
    ProviderDetails providerDetails = new ProviderDetails();
    CloudInfo cloudInfo = new CloudInfo();
    cloudInfo.aws = new AWSCloudInfo();
    cloudInfo.aws.setAwsAccessKeyID("accessKey");
    cloudInfo.aws.setAwsAccessKeySecret("accessKeySecret");
    providerDetails.setCloudInfo(cloudInfo);
    defaultProvider.setDetails(providerDetails);
    defaultNlbHealthCheckConfiguration =
        new NLBHealthCheckConfiguration(Arrays.asList(5433), Protocol.TCP, Arrays.asList());
  }

  @Test
  public void ensureConnectionTerminationOnDeregistrationEnabled() {
    Mockito.doReturn(mockElbClient).when(awsCloudImpl).getELBClient(any(), anyString());
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    Mockito.doReturn(Arrays.asList("")).when(awsCloudImpl).getInstanceIDs(any(), any());
    // Mockito.doNothing().when(awsCloudImpl).ensureLoadBalancerAttributes(any(), any());
    Mockito.doReturn(Listener.builder().listenerArn("listener1").build())
        .when(awsCloudImpl)
        .getListenerByPort(any(), any(), anyInt());
    Mockito.doReturn("tg-test").when(awsCloudImpl).getListenerTargetGroup(any());
    Mockito.doNothing().when(awsCloudImpl).ensureTargetGroupAttributes(any(), any());
    Mockito.doNothing()
        .when(awsCloudImpl)
        .checkNodeGroup(any(), any(), any(), anyInt(), any(), any());
    awsCloudImpl.manageNodeGroup(
        defaultProvider,
        defaultRegion.getCode(),
        "lb-test",
        new HashMap<AvailabilityZone, Set<NodeID>>(),
        Arrays.asList(5433),
        defaultNlbHealthCheckConfiguration);
    verify(awsCloudImpl).ensureTargetGroupAttributes(mockElbClient, "tg-test");
  }

  @Test
  public void testKeysExists() {
    assertTrue(awsCloudImpl.checkKeysExists(defaultProvider));
    defaultProvider.getDetails().getCloudInfo().aws.awsAccessKeyID = null;
    defaultProvider.getDetails().getCloudInfo().aws.awsAccessKeySecret = null;
    assertFalse(awsCloudImpl.checkKeysExists(defaultProvider));
  }

  @Test
  public void testDescribeVpc() {
    defaultRegion.setVnetName("vpc_id");
    Vpc vpc = Vpc.builder().build();
    DescribeVpcsResponse result =
        DescribeVpcsResponse.builder().vpcs(Collections.singletonList(vpc)).build();
    when(mockEC2Client.describeVpcs(any(DescribeVpcsRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Not found").build())
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeVpcOrBadRequest(defaultProvider, defaultRegion));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Vpc details extraction failed: Not found", exception.getMessage());
    assertEquals(vpc, awsCloudImpl.describeVpcOrBadRequest(defaultProvider, defaultRegion));
  }

  @Test
  public void testDescribeSubnet() {
    Subnet subnet = Subnet.builder().build();
    DescribeSubnetsResponse result =
        DescribeSubnetsResponse.builder().subnets(Collections.singletonList(subnet)).build();
    when(mockEC2Client.describeSubnets(any(DescribeSubnetsRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Not found").build())
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeSubnetsOrBadRequest(defaultProvider, defaultRegion));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Subnet details extraction failed: Not found", exception.getMessage());
    assertEquals(
        result.subnets(), awsCloudImpl.describeSubnetsOrBadRequest(defaultProvider, defaultRegion));
  }

  @Test
  public void testDescribeSecurityGroup() {
    defaultRegion.setSecurityGroupId("sg_id, sg_id_2");
    DescribeSecurityGroupsResponse result =
        DescribeSecurityGroupsResponse.builder()
            .securityGroups(SecurityGroup.builder().build(), SecurityGroup.builder().build())
            .build();
    when(mockEC2Client.describeSecurityGroups(any(DescribeSecurityGroupsRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Not found").build())
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeSecurityGroupsOrBadRequest(defaultProvider, defaultRegion));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Security group extraction failed: Not found", exception.getMessage());
    assertEquals(
        result.securityGroups(),
        awsCloudImpl.describeSecurityGroupsOrBadRequest(defaultProvider, defaultRegion));
  }

  @Test
  public void testDescribeImage() {
    String imageId = "image_id";
    defaultRegion.setYbImage(imageId);
    Image image = Image.builder().build();
    DescribeImagesResponse result = DescribeImagesResponse.builder().images(image).build();
    when(mockEC2Client.describeImages(any(DescribeImagesRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Not found").build())
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeImageOrBadRequest(defaultProvider, defaultRegion, imageId));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("AMI details extraction failed: Not found", exception.getMessage());
    assertEquals(
        image, awsCloudImpl.describeImageOrBadRequest(defaultProvider, defaultRegion, imageId));
  }

  @Test
  public void testHostedZone() {
    String hostedZoneId = "hosted_zone_id";
    defaultProvider.getDetails().getCloudInfo().aws.awsHostedZoneId = hostedZoneId;
    GetHostedZoneResponse result = GetHostedZoneResponse.builder().build();
    when(mockRoute53Client.getHostedZone(any(GetHostedZoneRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Not found").build())
        .thenReturn(result);
    Mockito.doReturn(mockRoute53Client).when(awsCloudImpl).getRoute53Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.getHostedZoneOrBadRequest(
                    defaultProvider, defaultRegion, hostedZoneId));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Hosted Zone validation failed: Not found", exception.getMessage());
    assertEquals(
        result,
        awsCloudImpl.getHostedZoneOrBadRequest(defaultProvider, defaultRegion, hostedZoneId));
  }

  @Test
  public void testDryRunDescribeInstance() {
    DescribeInstancesRequest dryRunRequest =
        DescribeInstancesRequest.builder().dryRun(true).build();
    when(mockEC2Client.describeInstances(eq(dryRunRequest)))
        .thenThrow(AwsServiceException.builder().message("Invalid details").build())
        .thenThrow(AwsServiceException.builder().message("Invalid region access").build())
        .thenReturn(
            software.amazon.awssdk.services.ec2.model.DescribeInstancesResponse.builder().build());
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstances failed: Invalid details", exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstances failed: Invalid region access", exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeInstanceOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeImage() {
    when(mockEC2Client.describeImages(any(DescribeImagesRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Invalid details").build())
        .thenThrow(AwsServiceException.builder().message("Invalid region access").build())
        .thenReturn(DescribeImagesResponse.builder().build());
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeImageOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Dry run of AWS DescribeImages failed: Invalid details", exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeImageOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeImages failed: Invalid region access", exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeImageOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeInstanceTypes() {
    when(mockEC2Client.describeInstanceTypes(any(DescribeInstanceTypesRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Invalid details").build())
        .thenThrow(AwsServiceException.builder().message("Invalid region access").build())
        .thenReturn(
            software.amazon.awssdk.services.ec2.model.DescribeInstanceTypesResponse.builder()
                .build());
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceTypesOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstanceTypes failed: Invalid details", exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceTypesOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstanceTypes failed: Invalid region access",
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeInstanceTypesOrBadRequest(
            defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeVpcs() {
    when(mockEC2Client.describeVpcs(any(DescribeVpcsRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Invalid details").build())
        .thenThrow(AwsServiceException.builder().message("Invalid region access").build())
        .thenReturn(DescribeVpcsResponse.builder().build());
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeVpcsOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Dry run of AWS DescribeVpcs failed: Invalid details", exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeVpcsOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeVpcs failed: Invalid region access", exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeVpcsOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeSubnets() {
    when(mockEC2Client.describeSubnets(any(DescribeSubnetsRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Invalid details").build())
        .thenThrow(AwsServiceException.builder().message("Invalid region access").build())
        .thenReturn(DescribeSubnetsResponse.builder().build());
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeSubnetOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("Dry run of AWS DescribeSubnets failed: Invalid details", exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeSubnetOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeSubnets failed: Invalid region access", exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeSubnetOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunSecurityGroup() {
    // Simulate normal AWS errors (not dry-run success)
    var invalidDetails =
        Ec2Exception.builder()
            .message("Invalid details")
            .statusCode(400)
            .awsErrorDetails(AwsErrorDetails.builder().errorCode("AuthFailure").build())
            .build();

    var invalidRegionAccess =
        Ec2Exception.builder()
            .message("Invalid region access")
            .statusCode(400)
            .awsErrorDetails(AwsErrorDetails.builder().errorCode("AuthFailure").build())
            .build();

    // Simulate dry-run success via exception
    var dryRunSuccess =
        Ec2Exception.builder()
            .message("Dry run would have succeeded")
            .statusCode(412)
            .awsErrorDetails(
                AwsErrorDetails.builder()
                    .errorCode("DryRunOperation")
                    .errorMessage("Request would have succeeded, but DryRun flag is set")
                    .build())
            .build();

    when(mockEC2Client.describeSecurityGroups(any(DescribeSecurityGroupsRequest.class)))
        .thenThrow(invalidDetails)
        .thenThrow(invalidRegionAccess)
        .thenThrow(dryRunSuccess);
    when(mockEC2Client.createSecurityGroup(any(CreateSecurityGroupRequest.class)))
        .thenThrow(invalidDetails)
        .thenThrow(invalidRegionAccess)
        .thenThrow(dryRunSuccess);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunSecurityGroupOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertTrue(
        exception.getMessage().contains("Dry run of AWS SecurityGroup failed: Invalid details"));
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunSecurityGroupOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertTrue(
        exception
            .getMessage()
            .contains("Dry run of AWS SecurityGroup failed: Invalid region access"));
    assertTrue(
        awsCloudImpl.dryRunSecurityGroupOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunKeyPair() {
    // Simulate normal AWS errors (not dry-run success)
    var invalidDetails =
        Ec2Exception.builder()
            .message("Invalid details")
            .statusCode(400)
            .awsErrorDetails(AwsErrorDetails.builder().errorCode("AuthFailure").build())
            .build();

    var invalidRegionAccess =
        Ec2Exception.builder()
            .message("Invalid region access")
            .statusCode(400)
            .awsErrorDetails(AwsErrorDetails.builder().errorCode("AuthFailure").build())
            .build();

    // Simulate dry-run success via exception
    var dryRunSuccess =
        Ec2Exception.builder()
            .message("Dry run would have succeeded")
            .statusCode(412)
            .awsErrorDetails(
                AwsErrorDetails.builder()
                    .errorCode("DryRunOperation")
                    .errorMessage("Request would have succeeded, but DryRun flag is set")
                    .build())
            .build();

    when(mockEC2Client.describeKeyPairs(any(DescribeKeyPairsRequest.class)))
        .thenThrow(invalidDetails)
        .thenThrow(invalidRegionAccess)
        .thenThrow(dryRunSuccess);
    when(mockEC2Client.createKeyPair(any(CreateKeyPairRequest.class)))
        .thenThrow(invalidDetails)
        .thenThrow(invalidRegionAccess)
        .thenThrow(dryRunSuccess);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.dryRunKeyPairOrBadRequest(defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertTrue(exception.getMessage().contains("Dry run of AWS KeyPair failed: Invalid details"));

    exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.dryRunKeyPairOrBadRequest(defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertTrue(
        exception.getMessage().contains("Dry run of AWS KeyPair failed: Invalid region access"));

    assertTrue(awsCloudImpl.dryRunKeyPairOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunAuthorizeSecurityGroupIngress() {
    when(mockEC2Client.authorizeSecurityGroupIngress(
            any(AuthorizeSecurityGroupIngressRequest.class)))
        .thenThrow(AwsServiceException.builder().message("Invalid details").build())
        .thenThrow(AwsServiceException.builder().message("Invalid region access").build())
        .thenReturn(
            software.amazon.awssdk.services.ec2.model.AuthorizeSecurityGroupIngressResponse
                .builder()
                .build());
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunAuthorizeSecurityGroupIngressOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS AuthorizeSecurityGroupIngress failed: Invalid details",
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunAuthorizeSecurityGroupIngressOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS AuthorizeSecurityGroupIngress failed: Invalid region access",
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunAuthorizeSecurityGroupIngressOrBadRequest(
            defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testSTSClient() {
    GetCallerIdentityResponse result = GetCallerIdentityResponse.builder().build();
    when(mockSTSService.getCallerIdentity(any(GetCallerIdentityRequest.class)))
        .thenThrow(SdkClientException.builder().message("Not found").build())
        .thenReturn(result);
    Mockito.doReturn(mockSTSService).when(awsCloudImpl).getStsClient(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.getStsClientOrBadRequest(defaultProvider, defaultRegion));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals("AWS access and secret keys validation failed: Not found", exception.getMessage());
    assertEquals(result, awsCloudImpl.getStsClientOrBadRequest(defaultProvider, defaultRegion));
  }

  @Test
  public void testPrivateKeyAlgo() {
    assertFalse(CertificateHelper.isValidRsaKey("random_key"));
    assertTrue(CertificateHelper.isValidRsaKey(CertificateHelperTest.getServerKeyContent()));
  }

  // Managed load balancer tests. The region has provider AZs us-east-1a and us-east-1b.

  private static final String LB_NAME = "lbi-h6uf6zcxc5cwfm74fsld6zvpuy";
  private static final String LB_ARN =
      "arn:aws:elasticloadbalancing:us-east-1:1:loadbalancer/net/x";

  private Region createLbRegion() {
    Region region = Region.create(defaultProvider, "us-east-1", "US East", "yb-image");
    AvailabilityZone.createOrThrow(region, "us-east-1a", "us-east-1a", "subnet-a");
    AvailabilityZone.createOrThrow(region, "us-east-1b", "us-east-1b", "subnet-b");
    Mockito.doReturn(mockElbClient).when(awsCloudImpl).getELBClient(any(), anyString());
    return region;
  }

  private static LoadBalancer existingNlb(String... zoneAndSubnet) {
    List<software.amazon.awssdk.services.elasticloadbalancingv2.model.AvailabilityZone> zones =
        new ArrayList<>();
    for (int i = 0; i < zoneAndSubnet.length; i += 2) {
      zones.add(
          software.amazon.awssdk.services.elasticloadbalancingv2.model.AvailabilityZone.builder()
              .zoneName(zoneAndSubnet[i])
              .subnetId(zoneAndSubnet[i + 1])
              .build());
    }
    return LoadBalancer.builder()
        .loadBalancerName(LB_NAME)
        .loadBalancerArn(LB_ARN)
        .dnsName(LB_NAME + ".elb.us-east-1.amazonaws.com")
        .type(LoadBalancerTypeEnum.NETWORK)
        .scheme(LoadBalancerSchemeEnum.INTERNAL)
        .state(LoadBalancerState.builder().code(LoadBalancerStateEnum.ACTIVE).build())
        .availabilityZones(zones)
        .build();
  }

  // Reads the zones back: Region.create() does not see the zones created after it.
  private List<AvailabilityZone> lbZones() {
    return Region.getByCode(defaultProvider, "us-east-1").getZones();
  }

  private void givenNlb(LoadBalancer lb) {
    when(mockElbClient.describeLoadBalancers(any(DescribeLoadBalancersRequest.class)))
        .thenReturn(DescribeLoadBalancersResponse.builder().loadBalancers(lb).build());
  }

  private void givenNoNlb() {
    when(mockElbClient.describeLoadBalancers(any(DescribeLoadBalancersRequest.class)))
        .thenThrow(LoadBalancerNotFoundException.builder().message("not found").build());
  }

  private static TargetGroup targetGroup(String name, String arn) {
    return TargetGroup.builder().targetGroupName(name).targetGroupArn(arn).build();
  }

  private static DescribeTargetGroupsResponse targetGroupPage(
      String nextMarker, TargetGroup... targetGroups) {
    return DescribeTargetGroupsResponse.builder()
        .targetGroups(targetGroups)
        .nextMarker(nextMarker)
        .build();
  }

  private static void assertCrossZoneEnabled(ModifyLoadBalancerAttributesRequest request) {
    assertEquals(LB_ARN, request.loadBalancerArn());
    assertEquals("load_balancing.cross_zone.enabled", request.attributes().get(0).key());
    assertEquals("true", request.attributes().get(0).value());
  }

  // manageNodeGroup on a load balancer without listeners: every port gets a target group and a
  // listener. The spy stubs the node lookup and the target group checks.
  private void givenNlbWithoutListeners() {
    createLbRegion();
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    Mockito.doReturn(Arrays.asList("i-1")).when(awsCloudImpl).getInstanceIDs(any(), any());
    Mockito.doReturn(null).when(awsCloudImpl).getListenerByPort(any(), any(), anyInt());
    Mockito.doNothing().when(awsCloudImpl).ensureTargetGroupAttributes(any(), any());
    Mockito.doNothing()
        .when(awsCloudImpl)
        .checkNodeGroup(any(), any(), any(), anyInt(), any(), any());
    givenNlb(existingNlb("us-east-1a", "subnet-a"));
    when(mockElbClient.createListener(any(CreateListenerRequest.class)))
        .thenReturn(CreateListenerResponse.builder().listeners(Listener.builder().build()).build());
  }

  @Test
  public void testEnsureLbCreatesNlbWithSubnetOfEveryProviderZone() {
    createLbRegion();
    givenNoNlb();
    when(mockElbClient.createLoadBalancer(any(CreateLoadBalancerRequest.class)))
        .thenReturn(
            CreateLoadBalancerResponse.builder()
                .loadBalancers(existingNlb("us-east-1a", "subnet-a", "us-east-1b", "subnet-b"))
                .build());

    awsCloudImpl.ensureManagedLoadBalancer(
        defaultProvider,
        "us-east-1",
        LB_NAME,
        lbZones(),
        List.of(5433),
        Map.of("universe-name", "u1"));

    ArgumentCaptor<CreateLoadBalancerRequest> create =
        ArgumentCaptor.forClass(CreateLoadBalancerRequest.class);
    verify(mockElbClient).createLoadBalancer(create.capture());
    assertEquals(LoadBalancerSchemeEnum.INTERNAL, create.getValue().scheme());
    assertEquals(LoadBalancerTypeEnum.NETWORK, create.getValue().type());
    // Zones get subnets even when they have no nodes yet.
    assertEquals(Set.of("subnet-a", "subnet-b"), new HashSet<>(create.getValue().subnets()));
    ArgumentCaptor<ModifyLoadBalancerAttributesRequest> attributes =
        ArgumentCaptor.forClass(ModifyLoadBalancerAttributesRequest.class);
    verify(mockElbClient).modifyLoadBalancerAttributes(attributes.capture());
    assertCrossZoneEnabled(attributes.getValue());
  }

  @Test
  public void testEnsureLbRejectsZonesWithoutSubnets() {
    Region region = Region.create(defaultProvider, "us-east-1", "US East", "yb-image");
    AvailabilityZone.createOrThrow(region, "us-east-1a", "us-east-1a", null);

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.ensureManagedLoadBalancer(
                    defaultProvider, "us-east-1", LB_NAME, lbZones(), List.of(5433), Map.of()));

    assertEquals(BAD_REQUEST, e.getHttpStatus());
  }

  @Test
  public void testEnsureLbReusesNlbCreatedAfterDescribe() {
    createLbRegion();
    // The NLB appears between the describe and the create, for example when the create call of an
    // earlier run timed out after AWS accepted it.
    LoadBalancer lb = existingNlb("us-east-1a", "subnet-a", "us-east-1b", "subnet-b");
    when(mockElbClient.describeLoadBalancers(any(DescribeLoadBalancersRequest.class)))
        .thenThrow(LoadBalancerNotFoundException.builder().message("not found").build())
        .thenReturn(DescribeLoadBalancersResponse.builder().loadBalancers(lb).build());
    when(mockElbClient.createLoadBalancer(any(CreateLoadBalancerRequest.class)))
        .thenThrow(DuplicateLoadBalancerNameException.builder().message("taken").build());

    String dnsName =
        awsCloudImpl.ensureManagedLoadBalancer(
            defaultProvider,
            "us-east-1",
            LB_NAME,
            lbZones(),
            List.of(5433),
            Map.of("universe-name", "u1"));

    assertEquals(lb.dnsName(), dnsName);
    ArgumentCaptor<ModifyLoadBalancerAttributesRequest> attributes =
        ArgumentCaptor.forClass(ModifyLoadBalancerAttributesRequest.class);
    verify(mockElbClient).modifyLoadBalancerAttributes(attributes.capture());
    assertCrossZoneEnabled(attributes.getValue());
  }

  @Test
  public void testEnsureLbSetsCrossZoneOnEveryRun() {
    createLbRegion();
    // The run that created the NLB can fail before it set the attribute.
    givenNlb(existingNlb("us-east-1a", "subnet-a", "us-east-1b", "subnet-b"));

    awsCloudImpl.ensureManagedLoadBalancer(
        defaultProvider,
        "us-east-1",
        LB_NAME,
        lbZones(),
        List.of(5433),
        Map.of("universe-name", "u1"));

    verify(mockElbClient, never()).createLoadBalancer(any(CreateLoadBalancerRequest.class));
    ArgumentCaptor<ModifyLoadBalancerAttributesRequest> attributes =
        ArgumentCaptor.forClass(ModifyLoadBalancerAttributesRequest.class);
    verify(mockElbClient).modifyLoadBalancerAttributes(attributes.capture());
    assertCrossZoneEnabled(attributes.getValue());
  }

  @Test
  public void testEnsureLbAddsSubnetOfNewZoneAndKeepsExistingOnes() {
    createLbRegion();
    // The provider zones are us-east-1a and us-east-1b. The NLB covers us-east-1a with another
    // subnet, which counts, because an NLB takes one subnet per zone. It also has a subnet in
    // us-east-1c, a zone that the provider no longer has.
    givenNlb(existingNlb("us-east-1a", "subnet-other", "us-east-1c", "subnet-c"));

    awsCloudImpl.ensureManagedLoadBalancer(
        defaultProvider,
        "us-east-1",
        LB_NAME,
        lbZones(),
        List.of(5433),
        Map.of("universe-name", "u1"));

    // SetSubnets replaces the list, and removing a subnet drops the connections in its zone.
    ArgumentCaptor<SetSubnetsRequest> setSubnets = ArgumentCaptor.forClass(SetSubnetsRequest.class);
    verify(mockElbClient).setSubnets(setSubnets.capture());
    assertEquals(
        Set.of("subnet-other", "subnet-b", "subnet-c"),
        new HashSet<>(setSubnets.getValue().subnets()));
  }

  @Test
  public void testDeleteLbDeletesTargetGroupsAfterLbAndRetriesWhileInUse() {
    createLbRegion();
    givenNlb(existingNlb("us-east-1a", "subnet-a"));
    Listener ysql =
        Listener.builder()
            .defaultActions(
                Action.builder().type(ActionTypeEnum.FORWARD).targetGroupArn("tg-ysql").build())
            .build();
    Listener ycql =
        Listener.builder()
            .defaultActions(
                Action.builder()
                    .type(ActionTypeEnum.FORWARD)
                    .forwardConfig(
                        ForwardActionConfig.builder()
                            .targetGroups(
                                TargetGroupTuple.builder().targetGroupArn("tg-ycql").build())
                            .build())
                    .build())
            .build();
    when(mockElbClient.describeListeners(any(DescribeListenersRequest.class)))
        .thenReturn(DescribeListenersResponse.builder().listeners(ysql, ycql).build());
    when(mockElbClient.deleteTargetGroup(any(DeleteTargetGroupRequest.class)))
        .thenThrow(ResourceInUseException.builder().message("in use by a listener").build())
        .thenReturn(null);
    when(mockElbClient.describeTargetGroups(any(DescribeTargetGroupsRequest.class)))
        .thenReturn(targetGroupPage(null, targetGroup("tg-other", "arn:other")));

    awsCloudImpl.deleteManagedLoadBalancer(defaultProvider, "us-east-1", LB_NAME);

    InOrder inOrder = Mockito.inOrder(mockElbClient);
    inOrder.verify(mockElbClient).deleteLoadBalancer(any(DeleteLoadBalancerRequest.class));
    ArgumentCaptor<DeleteTargetGroupRequest> deletes =
        ArgumentCaptor.forClass(DeleteTargetGroupRequest.class);
    inOrder.verify(mockElbClient, times(3)).deleteTargetGroup(deletes.capture());
    assertEquals(
        Arrays.asList("tg-ysql", "tg-ysql", "tg-ycql"),
        deletes.getAllValues().stream()
            .map(DeleteTargetGroupRequest::targetGroupArn)
            .collect(Collectors.toList()));
  }

  @Test
  public void testDeleteLbDeletesTargetGroupsNamedForLbWhenLbIsGone() {
    createLbRegion();
    // A failed run deleted the NLB and left its target groups behind.
    givenNoNlb();
    String ysql = AWSCloudImpl.getTargetGroupName(LB_NAME, 5433);
    String ycql = AWSCloudImpl.getTargetGroupName(LB_NAME, 9042);
    when(mockElbClient.describeTargetGroups(any(DescribeTargetGroupsRequest.class)))
        .thenReturn(
            targetGroupPage("page-2", targetGroup(ysql, "arn:ysql"), targetGroup("tg-x", "arn:x")))
        .thenReturn(targetGroupPage(null, targetGroup(ycql, "arn:ycql")));

    awsCloudImpl.deleteManagedLoadBalancer(defaultProvider, "us-east-1", LB_NAME);

    verify(mockElbClient, never()).deleteLoadBalancer(any(DeleteLoadBalancerRequest.class));
    ArgumentCaptor<DescribeTargetGroupsRequest> describes =
        ArgumentCaptor.forClass(DescribeTargetGroupsRequest.class);
    verify(mockElbClient, times(2)).describeTargetGroups(describes.capture());
    assertEquals("page-2", describes.getAllValues().get(1).marker());
    ArgumentCaptor<DeleteTargetGroupRequest> deletes =
        ArgumentCaptor.forClass(DeleteTargetGroupRequest.class);
    verify(mockElbClient, times(2)).deleteTargetGroup(deletes.capture());
    assertEquals(
        Arrays.asList("arn:ysql", "arn:ycql"),
        deletes.getAllValues().stream()
            .map(DeleteTargetGroupRequest::targetGroupArn)
            .collect(Collectors.toList()));
  }

  @Test
  public void testManageNodeGroupNamesTargetGroupAfterLbAndPort() {
    givenNlbWithoutListeners();
    when(mockElbClient.createTargetGroup(any(CreateTargetGroupRequest.class)))
        .thenReturn(
            CreateTargetGroupResponse.builder().targetGroups(targetGroup("tg", "arn:tg")).build());

    awsCloudImpl.manageNodeGroup(
        defaultProvider,
        "us-east-1",
        LB_NAME,
        new HashMap<>(),
        Arrays.asList(5433, 9042),
        defaultNlbHealthCheckConfiguration);

    ArgumentCaptor<CreateTargetGroupRequest> creates =
        ArgumentCaptor.forClass(CreateTargetGroupRequest.class);
    verify(mockElbClient, times(2)).createTargetGroup(creates.capture());
    List<String> names =
        creates.getAllValues().stream()
            .map(CreateTargetGroupRequest::name)
            .collect(Collectors.toList());
    assertEquals(
        Arrays.asList(
            AWSCloudImpl.getTargetGroupName(LB_NAME, 5433),
            AWSCloudImpl.getTargetGroupName(LB_NAME, 9042)),
        names);
    for (String name : names) {
      // The AWS limit is 32 characters.
      assertTrue(name, name.matches("tg-[0-9a-f]{22}-(5433|9042)"));
    }
    // The create can return a target group of an earlier run, so the nodes are reconciled.
    verify(awsCloudImpl, times(2))
        .checkNodeGroup(
            eq(mockElbClient), eq("arn:tg"), eq("TCP"), anyInt(), eq(Arrays.asList("i-1")), any());
  }

  @Test
  public void testManageNodeGroupReusesTargetGroupWhoseNameIsTaken() {
    givenNlbWithoutListeners();
    String name = AWSCloudImpl.getTargetGroupName(LB_NAME, 5433);
    // An earlier run left the target group behind with another health check.
    when(mockElbClient.createTargetGroup(any(CreateTargetGroupRequest.class)))
        .thenThrow(DuplicateTargetGroupNameException.builder().message("taken").build());
    when(mockElbClient.describeTargetGroups(any(DescribeTargetGroupsRequest.class)))
        .thenReturn(targetGroupPage(null, targetGroup(name, "arn:old")));

    awsCloudImpl.manageNodeGroup(
        defaultProvider,
        "us-east-1",
        LB_NAME,
        new HashMap<>(),
        Arrays.asList(5433),
        defaultNlbHealthCheckConfiguration);

    ArgumentCaptor<DescribeTargetGroupsRequest> describe =
        ArgumentCaptor.forClass(DescribeTargetGroupsRequest.class);
    verify(mockElbClient).describeTargetGroups(describe.capture());
    assertEquals(Arrays.asList(name), describe.getValue().names());
    verify(awsCloudImpl)
        .checkNodeGroup(
            eq(mockElbClient), eq("arn:old"), eq("TCP"), eq(5433), eq(Arrays.asList("i-1")), any());
    ArgumentCaptor<CreateListenerRequest> listener =
        ArgumentCaptor.forClass(CreateListenerRequest.class);
    verify(mockElbClient).createListener(listener.capture());
    assertEquals(
        "arn:old",
        listener
            .getValue()
            .defaultActions()
            .get(0)
            .forwardConfig()
            .targetGroups()
            .get(0)
            .targetGroupArn());
  }
}
