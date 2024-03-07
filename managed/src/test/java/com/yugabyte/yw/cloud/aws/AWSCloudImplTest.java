package com.yugabyte.yw.cloud.aws;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.amazonaws.AmazonServiceException;
import com.amazonaws.SdkClientException;
import com.amazonaws.services.ec2.AmazonEC2;
import com.amazonaws.services.ec2.model.AuthorizeSecurityGroupIngressRequest;
import com.amazonaws.services.ec2.model.CreateKeyPairRequest;
import com.amazonaws.services.ec2.model.CreateSecurityGroupRequest;
import com.amazonaws.services.ec2.model.DescribeImagesRequest;
import com.amazonaws.services.ec2.model.DescribeImagesResult;
import com.amazonaws.services.ec2.model.DescribeInstanceTypesRequest;
import com.amazonaws.services.ec2.model.DescribeInstancesRequest;
import com.amazonaws.services.ec2.model.DescribeKeyPairsRequest;
import com.amazonaws.services.ec2.model.DescribeSecurityGroupsRequest;
import com.amazonaws.services.ec2.model.DescribeSecurityGroupsResult;
import com.amazonaws.services.ec2.model.DescribeSubnetsRequest;
import com.amazonaws.services.ec2.model.DescribeSubnetsResult;
import com.amazonaws.services.ec2.model.DescribeVpcsRequest;
import com.amazonaws.services.ec2.model.DescribeVpcsResult;
import com.amazonaws.services.ec2.model.DryRunResult;
import com.amazonaws.services.ec2.model.Image;
import com.amazonaws.services.ec2.model.SecurityGroup;
import com.amazonaws.services.ec2.model.Subnet;
import com.amazonaws.services.ec2.model.Vpc;
import com.amazonaws.services.elasticloadbalancingv2.AmazonElasticLoadBalancing;
import com.amazonaws.services.elasticloadbalancingv2.model.Listener;
import com.amazonaws.services.route53.AmazonRoute53;
import com.amazonaws.services.route53.model.GetHostedZoneResult;
import com.amazonaws.services.securitytoken.AWSSecurityTokenService;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityResult;
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
import java.util.List;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.Mockito;

public class AWSCloudImplTest extends FakeDBApplication {

  private AWSCloudImpl awsCloudImpl;
  private Customer customer;
  private Provider defaultProvider;
  private NLBHealthCheckConfiguration defaultNlbHealthCheckConfiguration;
  private Region defaultRegion;
  @Mock private AmazonEC2 mockEC2Client;
  @Mock private AmazonRoute53 mockRoute53Client;
  @Mock private AWSSecurityTokenService mockSTSService;
  @Mock private AmazonElasticLoadBalancing mockElbClient;

  private String AMAZON_COMMON_ERROR_MSG =
      "(Service: null; Status Code: 0;" + " Error Code: null; Request ID: null; Proxy: null)";

  @Before
  public void setup() {
    awsCloudImpl = spy(new AWSCloudImpl(null));
    mockEC2Client = mock(AmazonEC2.class);
    mockElbClient = mock(AmazonElasticLoadBalancing.class);
    mockRoute53Client = mock(AmazonRoute53.class);
    mockSTSService = mock(AWSSecurityTokenService.class);
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
    Mockito.doReturn(new Listener().withListenerArn("listener1"))
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
    DescribeVpcsResult result = new DescribeVpcsResult();
    Vpc vpc = new Vpc();
    result.setVpcs(Collections.singletonList(vpc));
    when(mockEC2Client.describeVpcs(any()))
        .thenThrow(new AmazonServiceException("Not found"))
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeVpcOrBadRequest(defaultProvider, defaultRegion));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Vpc details extraction failed: Not found " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(vpc, awsCloudImpl.describeVpcOrBadRequest(defaultProvider, defaultRegion));
  }

  @Test
  public void testDescribeSubnet() {
    DescribeSubnetsResult result = new DescribeSubnetsResult();
    Subnet subnet = new Subnet();
    result.setSubnets(Collections.singletonList(subnet));
    when(mockEC2Client.describeSubnets(any()))
        .thenThrow(new AmazonServiceException("Not found"))
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeSubnetsOrBadRequest(defaultProvider, defaultRegion));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Subnet details extraction failed: Not found " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        result.getSubnets(),
        awsCloudImpl.describeSubnetsOrBadRequest(defaultProvider, defaultRegion));
  }

  @Test
  public void testDescribeSecurityGroup() {
    defaultRegion.setSecurityGroupId("sg_id, sg_id_2");
    DescribeSecurityGroupsResult result = new DescribeSecurityGroupsResult();
    List<SecurityGroup> securityGroupList = Arrays.asList(new SecurityGroup(), new SecurityGroup());
    result.setSecurityGroups(securityGroupList);
    when(mockEC2Client.describeSecurityGroups(any()))
        .thenThrow(new AmazonServiceException("Not found"))
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeSecurityGroupsOrBadRequest(defaultProvider, defaultRegion));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Security group extraction failed: Not found " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        securityGroupList,
        awsCloudImpl.describeSecurityGroupsOrBadRequest(defaultProvider, defaultRegion));
  }

  @Test
  public void testDescribeImage() {
    String imageId = "image_id";
    defaultRegion.setYbImage(imageId);
    DescribeImagesResult result = new DescribeImagesResult();
    Image image = new Image();
    result.setImages(Collections.singletonList(image));
    when(mockEC2Client.describeImages(any()))
        .thenThrow(new AmazonServiceException("Not found"))
        .thenReturn(result);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.describeImageOrBadRequest(defaultProvider, defaultRegion, imageId));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "AMI details extraction failed: Not found " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        image, awsCloudImpl.describeImageOrBadRequest(defaultProvider, defaultRegion, imageId));
  }

  @Test
  public void testHostedZone() {
    String hostedZoneId = "hosted_zone_id";
    defaultProvider.getDetails().getCloudInfo().aws.awsHostedZoneId = hostedZoneId;
    GetHostedZoneResult result = new GetHostedZoneResult();
    when(mockRoute53Client.getHostedZone(any()))
        .thenThrow(new AmazonServiceException("Not found"))
        .thenReturn(result);
    Mockito.doReturn(mockRoute53Client).when(awsCloudImpl).getRoute53Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.getHostedZoneOrBadRequest(
                    defaultProvider, defaultRegion, hostedZoneId));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Hosted Zone validation failed: Not found " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        result,
        awsCloudImpl.getHostedZoneOrBadRequest(defaultProvider, defaultRegion, hostedZoneId));
  }

  @Test
  public void testDryRunDescribeInstance() {
    DryRunResult<DescribeInstancesRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<DescribeInstancesRequest> dryRunResult2 =
        new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new DescribeInstancesRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstances failed: Invalid details " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstances failed: Invalid region access " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeInstanceOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeImage() {
    DryRunResult<DescribeImagesRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<DescribeImagesRequest> dryRunResult2 = new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new DescribeImagesRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeImageOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeImages failed: Invalid details " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeImageOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeImages failed: Invalid region access " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeImageOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeInstanceTypes() {
    DryRunResult<DescribeInstanceTypesRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<DescribeInstanceTypesRequest> dryRunResult2 =
        new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new DescribeInstanceTypesRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceTypesOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstanceTypes failed: Invalid details " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeInstanceTypesOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeInstanceTypes failed: Invalid region access "
            + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeInstanceTypesOrBadRequest(
            defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeVpcs() {
    DryRunResult<DescribeVpcsRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<DescribeVpcsRequest> dryRunResult2 = new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new DescribeVpcsRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeVpcsOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeVpcs failed: Invalid details " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeVpcsOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeVpcs failed: Invalid region access " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeVpcsOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunDescribeSubnets() {
    DryRunResult<DescribeSubnetsRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<DescribeSubnetsRequest> dryRunResult2 = new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new DescribeSubnetsRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeSubnetOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeSubnets failed: Invalid details " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunDescribeSubnetOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS DescribeSubnets failed: Invalid region access " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunDescribeSubnetOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunSecurityGroup() {
    DryRunResult<DescribeSecurityGroupsRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<DescribeSecurityGroupsRequest> dryRunResult2 =
        new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new DescribeSecurityGroupsRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    when(mockEC2Client.dryRun(new CreateSecurityGroupRequest()))
        .thenReturn(
            new DryRunResult<>(
                false, null, null, new AmazonServiceException("Invalid region access")))
        .thenReturn(new DryRunResult<>(true, null, null, null));
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunSecurityGroupOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS SecurityGroup failed: Invalid details " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunSecurityGroupOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS SecurityGroup failed: Invalid region access " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunSecurityGroupOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunKeyPair() {
    DryRunResult<DescribeKeyPairsRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<DescribeKeyPairsRequest> dryRunResult2 =
        new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new DescribeKeyPairsRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    when(mockEC2Client.dryRun(new CreateKeyPairRequest()))
        .thenReturn(
            new DryRunResult<>(
                false, null, null, new AmazonServiceException("Invalid region access")))
        .thenReturn(new DryRunResult<>(true, null, null, null));
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.dryRunKeyPairOrBadRequest(defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS KeyPair failed: Invalid details " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () -> awsCloudImpl.dryRunKeyPairOrBadRequest(defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS KeyPair failed: Invalid region access " + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true, awsCloudImpl.dryRunKeyPairOrBadRequest(defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testDryRunAuthorizeSecurityGroupIngress() {
    DryRunResult<AuthorizeSecurityGroupIngressRequest> dryRunResult =
        new DryRunResult<>(false, null, null, new AmazonServiceException("Invalid region access"));
    DryRunResult<AuthorizeSecurityGroupIngressRequest> dryRunResult2 =
        new DryRunResult<>(true, null, null, null);
    when(mockEC2Client.dryRun(new AuthorizeSecurityGroupIngressRequest()))
        .thenThrow(new AmazonServiceException("Invalid details"))
        .thenReturn(dryRunResult)
        .thenReturn(dryRunResult2);
    Mockito.doReturn(mockEC2Client).when(awsCloudImpl).getEC2Client(any(), anyString());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunAuthorizeSecurityGroupIngressOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS AuthorizeSecurityGroupIngress failed: Invalid details "
            + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                awsCloudImpl.dryRunAuthorizeSecurityGroupIngressOrBadRequest(
                    defaultProvider, defaultRegion.getCode()));
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "Dry run of AWS AuthorizeSecurityGroupIngress failed: Invalid region access "
            + AMAZON_COMMON_ERROR_MSG,
        exception.getMessage());
    assertEquals(
        true,
        awsCloudImpl.dryRunAuthorizeSecurityGroupIngressOrBadRequest(
            defaultProvider, defaultRegion.getCode()));
  }

  @Test
  public void testSTSClient() {
    GetCallerIdentityResult result = new GetCallerIdentityResult();
    when(mockSTSService.getCallerIdentity(any()))
        .thenThrow(new SdkClientException("Not found"))
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
