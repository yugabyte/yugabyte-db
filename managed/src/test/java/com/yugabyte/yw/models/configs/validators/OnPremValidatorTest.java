// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import java.util.Arrays;
import java.util.List;
import org.junit.Before;
import org.junit.Test;

/** Covers the federated-IAM target validation on an on-prem provider. */
public class OnPremValidatorTest extends FakeDBApplication {

  private static final String AUDIENCE =
      "//iam.googleapis.com/projects/123456789/locations/global/workloadIdentityPools/p/prov";
  private static final String ROLE_ARN = "arn:aws:iam::123456789012:role/yb-s3-fed-role";

  private Provider provider;
  private OnPremValidator validator;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    validator = app.injector().instanceOf(OnPremValidator.class);
  }

  private OnPremCloudInfo onPrem() {
    ProviderDetails details = provider.getDetails();
    if (details.getCloudInfo() == null) {
      details.setCloudInfo(new ProviderDetails.CloudInfo());
    }
    OnPremCloudInfo info = details.getCloudInfo().getOnprem();
    if (info == null) {
      info = new OnPremCloudInfo();
      details.getCloudInfo().setOnprem(info);
    }
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

  private String validateAndGetMessage() {
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> validator.validate(provider));
    return e.getMessage();
  }

  /**
   * The regression guard for every on-prem provider that exists today: with the feature off, the
   * validation must not look at - or reject - anything.
   */
  @Test
  public void testFederationDisabledIsNotValidated() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = false;
    info.federatedIamAudience = "not a valid audience!!";
    info.crossCloudFederationTargets = List.of(target(CloudType.aws, "bad audience", "garbage"));
    validator.validate(provider);
  }

  /** A provider configured before crossCloudFederationTargets existed still validates unchanged. */
  @Test
  public void testLegacyFlatAudienceIsAccepted() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.federatedIamAudience = AUDIENCE;
    validator.validate(provider);
  }

  @Test
  public void testGcpTargetIsAccepted() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.gcp, AUDIENCE, null));
    validator.validate(provider);
  }

  @Test
  public void testBothTargetsAreAccepted() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets =
        Arrays.asList(
            target(CloudType.gcp, AUDIENCE, null), target(CloudType.aws, AUDIENCE, ROLE_ARN));
    validator.validate(provider);
  }

  @Test
  public void testEnabledWithNoTargetIsRejected() {
    onPrem().enableFederatedIam = true;
    assertTrue(validateAndGetMessage().contains("no target is configured"));
  }

  /** An aws target is unusable without a role, so it must fail rather than be silently ignored. */
  @Test
  public void testAwsTargetWithoutRoleArnIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.aws, AUDIENCE, null));
    assertTrue(validateAndGetMessage().contains("role ARN"));
  }

  @Test
  public void testTargetWithoutAudienceIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.gcp, null, null));
    assertTrue(validateAndGetMessage().contains("audience"));
  }

  @Test
  public void testMalformedRoleArnIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets =
        List.of(target(CloudType.aws, AUDIENCE, "arn:aws:iam::12345:role/short-account"));
    assertTrue(validateAndGetMessage().contains("role ARN"));
  }

  /**
   * Both YBA and the node use the global STS endpoint, which the aws-cn and aws-us-gov partitions
   * do not serve, so an ARN from those partitions must not be accepted.
   */
  @Test
  public void testNonStandardPartitionRoleArnIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets =
        List.of(target(CloudType.aws, AUDIENCE, "arn:aws-cn:iam::123456789012:role/yb-fed"));
    assertTrue(validateAndGetMessage().contains("role ARN"));
  }

  @Test
  public void testMalformedAudienceIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets =
        List.of(target(CloudType.gcp, "has spaces and $pecials", null));
    assertTrue(validateAndGetMessage().contains("audience"));
  }

  /** The node picks its entry by target cloud alone, so two entries for one cloud are ambiguous. */
  @Test
  public void testDuplicateTargetCloudIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets =
        Arrays.asList(target(CloudType.gcp, AUDIENCE, null), target(CloudType.gcp, AUDIENCE, null));
    assertTrue(validateAndGetMessage().contains("Duplicate"));
  }

  /** Only aws and gcp are supported targets today; azu/oci must be rejected, not ignored. */
  @Test
  public void testUnsupportedTargetCloudIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.azu, AUDIENCE, null));
    assertTrue(validateAndGetMessage().contains("Unsupported"));
  }
}
