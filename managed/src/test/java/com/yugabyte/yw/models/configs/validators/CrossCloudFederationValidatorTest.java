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
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.util.Arrays;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;

/**
 * Federated-IAM target validation on the AWS and GCP providers. On-prem is covered by {@link
 * OnPremValidatorTest}; all three share one implementation, so the point here is that the same
 * payload is judged identically whichever provider carries it.
 */
public class CrossCloudFederationValidatorTest extends FakeDBApplication {

  private static final String AUDIENCE =
      "//iam.googleapis.com/projects/123456789/locations/global/workloadIdentityPools/p/prov";
  private static final String ROLE_ARN = "arn:aws:iam::123456789012:role/yb-s3-fed-role";

  private Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  private static CrossCloudFederationTarget target(
      CloudType targetCloud, String audience, String roleArn) {
    CrossCloudFederationTarget t = new CrossCloudFederationTarget();
    t.targetCloud = targetCloud;
    t.audience = audience;
    t.roleArn = roleArn;
    return t;
  }

  private Provider gcpProvider() {
    Provider provider = ModelFactory.gcpProvider(customer);
    GCPCloudInfo info = CloudInfoInterface.get(provider);
    info.setEnableFederatedIam(true);
    return provider;
  }

  private Provider awsProvider() {
    Provider provider = ModelFactory.awsProvider(customer);
    AWSCloudInfo info = CloudInfoInterface.get(provider);
    info.enableFederatedIam = true;
    return provider;
  }

  private String validateAndGetMessage(Provider provider, ProviderFieldsValidator validator) {
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> validator.validate(provider));
    return e.getMessage();
  }

  /**
   * The bug this guards: the GCP validator used to read only the deprecated flat
   * federatedIamAudience/RoleArn, so a provider saved by the current UI - which sends
   * crossCloudFederationTargets and nothing flat - was rejected as "role ARN is missing".
   */
  @Test
  public void testGcpProviderAcceptsTargetList() {
    Provider provider = gcpProvider();
    GCPCloudInfo info = CloudInfoInterface.get(provider);
    info.setCrossCloudFederationTargets(
        Collections.singletonList(target(CloudType.aws, AUDIENCE, ROLE_ARN)));
    // Must not throw.
    app.injector().instanceOf(GCPProviderValidator.class).validate(provider);
  }

  @Test
  public void testAwsProviderAcceptsTargetList() {
    Provider provider = awsProvider();
    AWSCloudInfo info = CloudInfoInterface.get(provider);
    info.crossCloudFederationTargets =
        Collections.singletonList(target(CloudType.gcp, AUDIENCE, null));
    app.injector().instanceOf(AWSProviderValidator.class).validate(provider);
  }

  /** A GCP provider still accepts a provider configured before the target list existed. */
  @Test
  public void testGcpProviderAcceptsLegacyFlatFields() {
    Provider provider = gcpProvider();
    GCPCloudInfo info = CloudInfoInterface.get(provider);
    info.setFederatedIamAudience(AUDIENCE);
    info.setFederatedIamRoleArn(ROLE_ARN);
    app.injector().instanceOf(GCPProviderValidator.class).validate(provider);
  }

  @Test
  public void testGcpProviderRejectsEnabledWithNoTarget() {
    Provider provider = gcpProvider();
    String message =
        validateAndGetMessage(provider, app.injector().instanceOf(GCPProviderValidator.class));
    assertTrue(message, message.contains("no target is configured"));
  }

  /** An S3 target without a role to assume is unusable, on any provider. */
  @Test
  public void testAwsTargetWithoutRoleArnIsRejected() {
    Provider provider = gcpProvider();
    GCPCloudInfo info = CloudInfoInterface.get(provider);
    info.setCrossCloudFederationTargets(
        Collections.singletonList(target(CloudType.aws, AUDIENCE, null)));
    String message =
        validateAndGetMessage(provider, app.injector().instanceOf(GCPProviderValidator.class));
    assertTrue(message, message.contains("role ARN"));
  }

  @Test
  public void testMalformedAudienceIsRejected() {
    Provider provider = awsProvider();
    AWSCloudInfo info = CloudInfoInterface.get(provider);
    info.crossCloudFederationTargets =
        Collections.singletonList(target(CloudType.gcp, "not a valid audience!!", null));
    String message =
        validateAndGetMessage(provider, app.injector().instanceOf(AWSProviderValidator.class));
    assertTrue(message, message.contains("audience"));
  }

  @Test
  public void testDuplicateTargetCloudIsRejected() {
    Provider provider = awsProvider();
    AWSCloudInfo info = CloudInfoInterface.get(provider);
    info.crossCloudFederationTargets =
        Arrays.asList(target(CloudType.gcp, AUDIENCE, null), target(CloudType.gcp, AUDIENCE, null));
    String message =
        validateAndGetMessage(provider, app.injector().instanceOf(AWSProviderValidator.class));
    assertTrue(message, message.contains("Duplicate"));
  }

  /**
   * With the feature off, nothing is inspected - the guard for every provider that exists today.
   */
  @Test
  public void testFederationDisabledIsNotValidated() {
    Provider provider = ModelFactory.gcpProvider(customer);
    GCPCloudInfo info = CloudInfoInterface.get(provider);
    info.setEnableFederatedIam(false);
    info.setCrossCloudFederationTargets(
        Collections.singletonList(target(CloudType.aws, "not a valid audience!!", null)));
    app.injector().instanceOf(GCPProviderValidator.class).validate(provider);
  }
}
