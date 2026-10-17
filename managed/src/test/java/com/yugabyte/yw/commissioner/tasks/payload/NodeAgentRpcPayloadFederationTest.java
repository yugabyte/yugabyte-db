// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.payload;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import com.yugabyte.yw.models.helpers.provider.OCICloudInfo;
import com.yugabyte.yw.nodeagent.ConfigureCloudFederationInput;
import com.yugabyte.yw.nodeagent.ConfigureCloudFederationInput.CloudProvider;
import org.junit.Before;
import org.junit.Test;

/** The direction-dependent part of the node-agent federation input. */
public class NodeAgentRpcPayloadFederationTest extends FakeDBApplication {

  private static final String AUDIENCE =
      "//iam.googleapis.com/projects/1/locations/global/workloadIdentityPools/p/providers/a";
  private static final String ROLE_ARN = "arn:aws:iam::123456789012:role/yb-s3-fed-role";
  private static final String DOMAIN_URL = "https://idcs-abc123.identity.oraclecloud.com";

  private Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  private static CrossCloudFederationTarget target(
      CloudType targetCloud, String audience, String roleArn) {
    return CrossCloudFederationTarget.of(targetCloud, audience, roleArn);
  }

  private static ProviderDetails.CloudInfo cloudInfo(Provider provider) {
    ProviderDetails details = provider.getDetails();
    if (details.getCloudInfo() == null) {
      details.setCloudInfo(new ProviderDetails.CloudInfo());
    }
    return details.getCloudInfo();
  }

  private Provider ociProvider(String domainUrl) {
    Provider provider = ModelFactory.ociProvider(customer);
    setOciIdentityDomain(provider, domainUrl);
    return provider;
  }

  /** Providers are unique per cloud and name, so a test that varies the URL reuses one provider. */
  private static void setOciIdentityDomain(Provider provider, String domainUrl) {
    OCICloudInfo info = new OCICloudInfo();
    info.ociIdentityDomainUrl = domainUrl;
    cloudInfo(provider).setOci(info);
  }

  private static ConfigureCloudFederationInput apply(
      Provider provider, CloudType source, CrossCloudFederationTarget target) {
    ConfigureCloudFederationInput.Builder builder = ConfigureCloudFederationInput.newBuilder();
    NodeAgentRpcPayload.applyFederationDirection(builder, provider, source, target);
    return builder.build();
  }

  /** Flow A: a node on OCI reaching GCS carries the identity domain it exchanges at. */
  @Test
  public void testOciNodeReachingGcs() {
    ConfigureCloudFederationInput input =
        apply(ociProvider(DOMAIN_URL), CloudType.oci, target(CloudType.gcp, AUDIENCE, null));

    assertEquals(CloudProvider.OCI, input.getSourceCloud());
    assertEquals(CloudProvider.GCP, input.getTargetCloud());
    assertEquals(AUDIENCE, input.getGcs().getAudience());
    assertTrue(input.hasOciSource());
    assertEquals(DOMAIN_URL, input.getOciSource().getIdentityDomainUrl());
  }

  /** The value is trimmed, so a pasted trailing space never reaches a unit file. */
  @Test
  public void testOciIdentityDomainIsTrimmed() {
    ConfigureCloudFederationInput input =
        apply(
            ociProvider("  " + DOMAIN_URL + "  "),
            CloudType.oci,
            target(CloudType.gcp, AUDIENCE, null));
    assertEquals(DOMAIN_URL, input.getOciSource().getIdentityDomainUrl());
  }

  /** Without the domain the node cannot get an identity, so say so rather than send nothing. */
  @Test
  public void testOciNodeWithoutIdentityDomainFails() {
    Provider provider = ociProvider(DOMAIN_URL);
    for (String missing : new String[] {null, "", "   "}) {
      setOciIdentityDomain(provider, missing);
      RuntimeException e =
          assertThrows(
              RuntimeException.class,
              () -> apply(provider, CloudType.oci, target(CloudType.gcp, AUDIENCE, null)));
      assertTrue(e.getMessage(), e.getMessage().contains("identity domain URL"));
    }
  }

  /** OCI to S3 has no flow, so no input may be produced for it. */
  @Test
  public void testOciToAwsIsNotSupported() {
    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () ->
                apply(
                    ociProvider(DOMAIN_URL),
                    CloudType.oci,
                    target(CloudType.aws, AUDIENCE, ROLE_ARN)));
    assertTrue(e.getMessage(), e.getMessage().contains("from oci to aws is not supported"));
  }

  /** Nothing reaches OCI storage yet. */
  @Test
  public void testNothingReachesOciStorage() {
    Provider provider = ModelFactory.awsProvider(customer);
    for (CloudType source : new CloudType[] {CloudType.aws, CloudType.gcp}) {
      RuntimeException e =
          assertThrows(
              RuntimeException.class,
              () -> apply(provider, source, target(CloudType.oci, AUDIENCE, null)));
      assertTrue(e.getMessage(), e.getMessage().contains("is not supported"));
    }
  }

  /** Regression: the AWS-node-to-GCS direction is unchanged and carries no OCI source. */
  @Test
  public void testAwsNodeReachingGcsIsUnchanged() {
    ConfigureCloudFederationInput input =
        apply(
            ModelFactory.awsProvider(customer),
            CloudType.aws,
            target(CloudType.gcp, AUDIENCE, null));

    assertEquals(CloudProvider.AWS, input.getSourceCloud());
    assertEquals(CloudProvider.GCP, input.getTargetCloud());
    assertEquals(AUDIENCE, input.getGcs().getAudience());
    assertFalse(input.hasOciSource());
  }

  /** Regression: the GCP-node-to-S3 direction is unchanged and carries no OCI source. */
  @Test
  public void testGcpNodeReachingS3IsUnchanged() {
    ConfigureCloudFederationInput input =
        apply(
            ModelFactory.gcpProvider(customer),
            CloudType.gcp,
            target(CloudType.aws, AUDIENCE, ROLE_ARN));

    assertEquals(CloudProvider.GCP, input.getSourceCloud());
    assertEquals(CloudProvider.AWS, input.getTargetCloud());
    assertEquals(ROLE_ARN, input.getS3().getRoleArn());
    assertEquals(AUDIENCE, input.getS3().getAudience());
    assertFalse(input.getS3().getProfileName().isEmpty());
    assertFalse(input.hasOciSource());
  }

  @Test
  public void testIncompleteTargetsFail() {
    assertThrows(
        RuntimeException.class,
        () -> apply(ociProvider(DOMAIN_URL), CloudType.oci, target(CloudType.gcp, "", null)));
    assertThrows(
        RuntimeException.class,
        () ->
            apply(
                ModelFactory.gcpProvider(customer),
                CloudType.gcp,
                target(CloudType.aws, AUDIENCE, null)));
  }
}
