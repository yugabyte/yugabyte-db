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
import static org.mockito.Mockito.spy;
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
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
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
import software.amazon.awssdk.services.elasticloadbalancingv2.model.Listener;
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
    awsCloudImpl = spy(new AWSCloudImpl(null));
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
}
