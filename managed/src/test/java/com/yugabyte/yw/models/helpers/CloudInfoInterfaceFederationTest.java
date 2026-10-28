// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;

/**
 * Covers which storage clouds a provider resolves federation settings for. The on-prem cases are
 * the reason the lookup is keyed by target cloud: that provider can reach more than one.
 */
public class CloudInfoInterfaceFederationTest extends FakeDBApplication {

  private static final String GCS_AUDIENCE = "//iam.googleapis.com/projects/1/gcs-target";
  private static final String S3_AUDIENCE = "//iam.googleapis.com/projects/1/s3-target";
  private static final String ROLE_ARN = "arn:aws:iam::123456789012:role/yb-s3-fed-role";

  private Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  private static ProviderDetails.CloudInfo cloudInfo(Provider provider) {
    ProviderDetails details = provider.getDetails();
    if (details.getCloudInfo() == null) {
      details.setCloudInfo(new ProviderDetails.CloudInfo());
    }
    return details.getCloudInfo();
  }

  private OnPremCloudInfo onPremInfo(Provider provider) {
    OnPremCloudInfo info = new OnPremCloudInfo();
    cloudInfo(provider).setOnprem(info);
    return info;
  }

  private static CrossCloudFederationTarget target(
      CloudType targetCloud, String audience, String roleArn) {
    CrossCloudFederationTarget t = new CrossCloudFederationTarget();
    t.targetCloud = targetCloud;
    t.audience = audience;
    t.roleArn = roleArn;
    return t;
  }

  private static Set<CloudType> targetClouds(Provider provider) {
    return CloudInfoInterface.getCrossCloudFederationTargets(provider).stream()
        .map(t -> t.targetCloud)
        .collect(Collectors.toSet());
  }

  @Test
  public void testOnPremDisabledResolvesNoTarget() {
    Provider provider = ModelFactory.onpremProvider(customer);
    OnPremCloudInfo info = onPremInfo(provider);
    info.enableFederatedIam = false;
    info.crossCloudFederationTargets = List.of(target(CloudType.gcp, GCS_AUDIENCE, null));
    assertTrue(CloudInfoInterface.getCrossCloudFederationTargets(provider).isEmpty());
  }

  /**
   * Back-compat guard: a provider configured before crossCloudFederationTargets existed carries
   * only the flat federatedIamAudience, which always meant GCS. If this breaks, every on-prem
   * universe federating today silently stops being configured.
   */
  @Test
  public void testOnPremLegacyFlatAudienceResolvesGcsTarget() {
    Provider provider = ModelFactory.onpremProvider(customer);
    OnPremCloudInfo info = onPremInfo(provider);
    info.enableFederatedIam = true;
    info.federatedIamAudience = GCS_AUDIENCE;

    assertEquals(Set.of(CloudType.gcp), targetClouds(provider));
    CrossCloudFederationTarget gcs =
        CloudInfoInterface.getCrossCloudFederationTarget(provider, CloudType.gcp);
    assertNotNull(gcs);
    assertEquals(GCS_AUDIENCE, gcs.audience);
    assertNull(CloudInfoInterface.getCrossCloudFederationTarget(provider, CloudType.aws));
  }

  @Test
  public void testOnPremExplicitTargetsWinOverLegacyFlatAudience() {
    Provider provider = ModelFactory.onpremProvider(customer);
    OnPremCloudInfo info = onPremInfo(provider);
    info.enableFederatedIam = true;
    info.federatedIamAudience = "stale-legacy-audience";
    info.crossCloudFederationTargets = List.of(target(CloudType.gcp, GCS_AUDIENCE, null));

    assertEquals(
        GCS_AUDIENCE,
        CloudInfoInterface.getCrossCloudFederationTarget(provider, CloudType.gcp).audience);
  }

  @Test
  public void testOnPremAwsTargetResolvesAudienceAndRoleArn() {
    Provider provider = ModelFactory.onpremProvider(customer);
    OnPremCloudInfo info = onPremInfo(provider);
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.aws, S3_AUDIENCE, ROLE_ARN));

    assertEquals(Set.of(CloudType.aws), targetClouds(provider));
    CrossCloudFederationTarget s3 =
        CloudInfoInterface.getCrossCloudFederationTarget(provider, CloudType.aws);
    assertNotNull(s3);
    assertEquals(S3_AUDIENCE, s3.audience);
    assertEquals(ROLE_ARN, s3.roleArn);
  }

  /** Reaching S3 is impossible without the role to assume, so the entry must not count. */
  @Test
  public void testOnPremAwsTargetWithoutRoleArnIsNotUsable() {
    Provider provider = ModelFactory.onpremProvider(customer);
    OnPremCloudInfo info = onPremInfo(provider);
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.aws, S3_AUDIENCE, null));

    assertTrue(CloudInfoInterface.getCrossCloudFederationTargets(provider).isEmpty());
  }

  /** The case the ticket exists for: one on-prem provider reaching both storage clouds. */
  @Test
  public void testOnPremCanCarryBothTargets() {
    Provider provider = ModelFactory.onpremProvider(customer);
    OnPremCloudInfo info = onPremInfo(provider);
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets =
        Arrays.asList(
            target(CloudType.gcp, GCS_AUDIENCE, null),
            target(CloudType.aws, S3_AUDIENCE, ROLE_ARN));

    assertEquals(Set.of(CloudType.gcp, CloudType.aws), targetClouds(provider));
    assertEquals(
        GCS_AUDIENCE,
        CloudInfoInterface.getCrossCloudFederationTarget(provider, CloudType.gcp).audience);
    assertEquals(
        ROLE_ARN,
        CloudInfoInterface.getCrossCloudFederationTarget(provider, CloudType.aws).roleArn);
  }

  /** A GCP provider's nodes are all on GCP, so the only cloud they can need is AWS. */
  @Test
  public void testGcpProviderResolvesOnlyAwsTarget() {
    Provider provider = ModelFactory.gcpProvider(customer);
    GCPCloudInfo info = new GCPCloudInfo();
    info.setEnableFederatedIam(true);
    info.setFederatedIamAudience(S3_AUDIENCE);
    info.setFederatedIamRoleArn(ROLE_ARN);
    cloudInfo(provider).setGcp(info);

    assertEquals(Set.of(CloudType.aws), targetClouds(provider));
    assertNull(CloudInfoInterface.getCrossCloudFederationTarget(provider, CloudType.gcp));
  }

  @Test
  public void testTargetIsUsableOnlyWhenItsCloudSpecificFieldsAreSet() {
    assertTrue(target(CloudType.gcp, GCS_AUDIENCE, null).isUsable());
    assertTrue(target(CloudType.aws, S3_AUDIENCE, ROLE_ARN).isUsable());
    // aws needs a role to assume; every target needs an audience.
    assertTrue(!target(CloudType.aws, S3_AUDIENCE, null).isUsable());
    assertTrue(!target(CloudType.gcp, null, null).isUsable());
    assertTrue(!target(null, GCS_AUDIENCE, null).isUsable());
  }
}
