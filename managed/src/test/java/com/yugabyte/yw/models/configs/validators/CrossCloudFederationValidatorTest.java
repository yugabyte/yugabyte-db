// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.OCICloudInfo;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import java.util.Arrays;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;

/**
 * Federated-IAM target validation on the AWS, GCP and OCI providers. On-prem is covered by {@link
 * OnPremValidatorTest}; all four share one implementation, so the point here is that the same
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
    // OCI is a federation source only once the operator turns it on; most cases here are about what
    // an OCI provider may then carry, so they run with it on and the gate has its own tests.
    setOciSourceEnabled(true);
  }

  private static void setOciSourceEnabled(boolean enabled) {
    RuntimeConfigEntry.upsertGlobal(
        GlobalConfKeys.enableOciFederationSource.getKey(), String.valueOf(enabled));
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

  private static final String DOMAIN_URL = "https://idcs-abc123.identity.oraclecloud.com";

  private Provider ociProvider(OCICloudInfo info) {
    Provider provider = ModelFactory.ociProvider(customer);
    ProviderDetails details = provider.getDetails();
    if (details.getCloudInfo() == null) {
      details.setCloudInfo(new ProviderDetails.CloudInfo());
    }
    details.getCloudInfo().setOci(info);
    return provider;
  }

  private OCICloudInfo ociInfo(String domainUrl, CrossCloudFederationTarget... targets) {
    OCICloudInfo info = new OCICloudInfo();
    info.enableFederatedIam = true;
    info.ociIdentityDomainUrl = domainUrl;
    info.crossCloudFederationTargets = java.util.Arrays.asList(targets);
    return info;
  }

  private Provider onPremProvider(CrossCloudFederationTarget... targets) {
    Provider provider = ModelFactory.onpremProvider(customer);
    ProviderDetails details = provider.getDetails();
    if (details.getCloudInfo() == null) {
      details.setCloudInfo(new ProviderDetails.CloudInfo());
    }
    OnPremCloudInfo info = new OnPremCloudInfo();
    info.enableFederatedIam = true;
    info.crossCloudFederationTargets = java.util.Arrays.asList(targets);
    details.getCloudInfo().setOnprem(info);
    return provider;
  }

  private String ociValidationMessage(Provider provider) {
    OCIProviderValidator validator = app.injector().instanceOf(OCIProviderValidator.class);
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> validator.validateCrossCloudFederationTargets(provider));
    return e.getMessage();
  }

  private void validateOci(Provider provider) {
    // The shared federation check only: the provider validator's live credential check calls OCI.
    app.injector()
        .instanceOf(OCIProviderValidator.class)
        .validateCrossCloudFederationTargets(provider);
  }

  /** The direction OCI nodes have: they can reach GCS through GCP Workload Identity Federation. */
  @Test
  public void testOciProviderAcceptsGcpTargetWithIdentityDomain() {
    validateOci(ociProvider(ociInfo(DOMAIN_URL, target(CloudType.gcp, AUDIENCE, null))));
  }

  /** The OCI console shows the domain URL with the port, so a pasted value carries it. */
  @Test
  public void testOciProviderAcceptsIdentityDomainWithPort() {
    validateOci(ociProvider(ociInfo(DOMAIN_URL + ":443", target(CloudType.gcp, AUDIENCE, null))));
  }

  /** OCI nodes cannot federate to S3, so naming it would leave them with no credentials for it. */
  @Test
  public void testOciProviderRejectsAwsTarget() {
    String message =
        ociValidationMessage(
            ociProvider(ociInfo(DOMAIN_URL, target(CloudType.aws, AUDIENCE, ROLE_ARN))));
    assertTrue(message, message.contains("aws"));
    assertTrue(message, message.contains("Supported: [gcp]"));
  }

  @Test
  public void testOciProviderRequiresIdentityDomainWhenFederationIsOn() {
    String message =
        ociValidationMessage(ociProvider(ociInfo(null, target(CloudType.gcp, AUDIENCE, null))));
    assertTrue(message, message.contains("no OCI identity domain URL is set"));
  }

  @Test
  public void testOciProviderRejectsMalformedIdentityDomain() {
    // Providers are unique per cloud and name, so one provider is reused across the cases.
    Provider provider = ociProvider(ociInfo(DOMAIN_URL, target(CloudType.gcp, AUDIENCE, null)));
    for (String bad :
        new String[] {
          "http://idcs-abc123.identity.oraclecloud.com",
          DOMAIN_URL + "/",
          DOMAIN_URL + "/oauth2/v1/token",
          "idcs-abc123.identity.oraclecloud.com",
        }) {
      provider
          .getDetails()
          .getCloudInfo()
          .setOci(ociInfo(bad, target(CloudType.gcp, AUDIENCE, null)));
      String message = ociValidationMessage(provider);
      assertTrue(bad + ": " + message, message.contains("must be an https URL"));
    }
  }

  @Test
  public void testOciProviderRejectsEnabledWithNoTarget() {
    String message = ociValidationMessage(ociProvider(ociInfo(DOMAIN_URL)));
    assertTrue(message, message.contains("no target is configured"));
  }

  /**
   * A node-agent that predates OCI sources would ignore the setting and report success, so the
   * operator has to confirm node-agent first; until then saving it would silently do nothing.
   */
  @Test
  public void testOciProviderIsRejectedWhileOciSourceIsNotEnabled() {
    setOciSourceEnabled(false);
    String message =
        ociValidationMessage(
            ociProvider(ociInfo(DOMAIN_URL, target(CloudType.gcp, AUDIENCE, null))));
    assertTrue(message, message.contains(GlobalConfKeys.enableOciFederationSource.getKey()));
  }

  /** The gate is about nodes on OCI: nothing else a provider can carry is affected by it. */
  @Test
  public void testOciGateDoesNotAffectOtherProviders() {
    setOciSourceEnabled(false);
    app.injector()
        .instanceOf(OnPremValidator.class)
        .validateCrossCloudFederationTargets(onPremProvider(target(CloudType.gcp, AUDIENCE, null)));
    validateOci(ociProvider(new OCICloudInfo()));
    app.injector().instanceOf(AWSProviderValidator.class).validate(awsProviderWithGcpTarget());
  }

  private Provider awsProviderWithGcpTarget() {
    Provider provider = awsProvider();
    AWSCloudInfo info = CloudInfoInterface.get(provider);
    info.crossCloudFederationTargets =
        Collections.singletonList(target(CloudType.gcp, AUDIENCE, null));
    return provider;
  }

  /** With the feature off nothing is inspected, as for every provider that exists today. */
  @Test
  public void testOciProviderWithFederationOffIsNotValidated() {
    OCICloudInfo info = ociInfo("not a url", target(CloudType.aws, "bad audience!!", null));
    info.enableFederatedIam = false;
    validateOci(ociProvider(info));
  }

  /** An OCI provider with no federation configured at all - every one that exists today. */
  @Test
  public void testExistingOciProviderIsUnaffected() {
    validateOci(ociProvider(new OCICloudInfo()));
  }

  /** Nothing can be reached on OCI by federation yet, whichever provider names it. */
  @Test
  public void testOciIsNotAValidTargetCloud() {
    Provider provider = awsProvider();
    AWSCloudInfo info = CloudInfoInterface.get(provider);
    info.crossCloudFederationTargets =
        Collections.singletonList(target(CloudType.oci, AUDIENCE, null));
    String message =
        validateAndGetMessage(provider, app.injector().instanceOf(AWSProviderValidator.class));
    assertTrue(message, message.contains("oci"));
  }

  /** On-prem keeps both clouds: its nodes may be on any of them. */
  @Test
  public void testOnPremStillAcceptsBothGcpAndAwsTargets() {
    app.injector()
        .instanceOf(OnPremValidator.class)
        .validateCrossCloudFederationTargets(
            onPremProvider(
                target(CloudType.gcp, AUDIENCE, null), target(CloudType.aws, AUDIENCE, ROLE_ARN)));
  }
}
