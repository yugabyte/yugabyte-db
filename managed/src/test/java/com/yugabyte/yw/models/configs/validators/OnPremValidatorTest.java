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
      "//iam.googleapis.com/projects/123456789/locations/global/workloadIdentityPools/p/"
          + "providers/prov";
  // What a GCS bucket's grant names: the principals of a pool, not the provider STS takes.
  private static final String PRINCIPAL_SET =
      "//iam.googleapis.com/projects/123456789/locations/global/workloadIdentityPools/p/"
          + "attribute.aws_role/yb-node-role";
  private static final String ROLE_ARN = "arn:aws:iam::123456789012:role/yb-s3-fed-role";

  private Customer customer;
  private Provider provider;
  private OnPremValidator validator;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    validator = app.injector().instanceOf(OnPremValidator.class);
  }

  private OnPremCloudInfo onPrem() {
    return onPrem(provider);
  }

  private static OnPremCloudInfo onPrem(Provider provider) {
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

  /** As the create (existing null) or edit of {@code existing} that ProviderValidator runs. */
  private String validateAndGetMessage(Provider existing) {
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> validator.validate(provider, existing));
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

  /** A gcp audience that is not the pool provider is refused by STS, only once a backup runs. */
  @Test
  public void testGcpTargetWithPrincipalSetAudienceIsRejected() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.gcp, PRINCIPAL_SET, null));
    assertTrue(validateAndGetMessage(null).contains("workload identity pool provider"));
  }

  /** An aws audience is whatever the role's trust policy expects, so its form is not checked. */
  @Test
  public void testAwsTargetAudienceFormIsNotChecked() {
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets =
        List.of(target(CloudType.aws, "https://example.com/yb-s3-federation", ROLE_ARN));
    validator.validate(provider, null);
  }

  /**
   * A provider saved before the check keeps its gcp audience through an edit of its other fields,
   * as one that universes use cannot change it; a changed audience is checked. The saved audience
   * is that of the provider being edited: the request here carries another provider's uuid.
   */
  @Test
  public void testSavedGcpAudienceIsKeptOnEdit() {
    Provider saved = ModelFactory.newProvider(customer, CloudType.onprem, "saved-onprem");
    OnPremCloudInfo savedInfo = onPrem(saved);
    savedInfo.enableFederatedIam = true;
    savedInfo.crossCloudFederationTargets = List.of(target(CloudType.gcp, PRINCIPAL_SET, null));
    OnPremCloudInfo info = onPrem();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = List.of(target(CloudType.gcp, PRINCIPAL_SET, null));
    validator.validate(provider, saved);

    info.crossCloudFederationTargets =
        List.of(target(CloudType.gcp, PRINCIPAL_SET + "-other", null));
    assertTrue(validateAndGetMessage(saved).contains("workload identity pool provider"));

    // A saved gcp entry without an audience has none to keep, and is no error of its own.
    savedInfo.crossCloudFederationTargets = List.of(target(CloudType.gcp, null, null));
    info.crossCloudFederationTargets = List.of(target(CloudType.gcp, PRINCIPAL_SET, null));
    assertTrue(validateAndGetMessage(saved).contains("workload identity pool provider"));
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
