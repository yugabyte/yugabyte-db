// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import java.util.Collections;
import java.util.Set;
import org.junit.Test;

/** The (node cloud -> storage cloud) pairs federation serves, and the rules about OCI's inputs. */
public class CrossCloudFederationRoutesTest {

  @Test
  public void testSupportedRoutes() {
    assertTrue(CrossCloudFederationRoutes.isSupported(CloudType.aws, CloudType.gcp));
    assertTrue(CrossCloudFederationRoutes.isSupported(CloudType.gcp, CloudType.aws));
    assertTrue(CrossCloudFederationRoutes.isSupported(CloudType.oci, CloudType.gcp));
  }

  /**
   * Every pair no flow serves must be refused: a node pointed at one would be configured with
   * credentials that cannot work.
   */
  @Test
  public void testUnsupportedRoutes() {
    // Nothing reaches OCI storage yet, and OCI nodes have no route to S3.
    assertFalse(CrossCloudFederationRoutes.isSupported(CloudType.aws, CloudType.oci));
    assertFalse(CrossCloudFederationRoutes.isSupported(CloudType.gcp, CloudType.oci));
    assertFalse(CrossCloudFederationRoutes.isSupported(CloudType.oci, CloudType.aws));
    // Same cloud is native, never federated.
    assertFalse(CrossCloudFederationRoutes.isSupported(CloudType.aws, CloudType.aws));
    assertFalse(CrossCloudFederationRoutes.isSupported(CloudType.gcp, CloudType.gcp));
    assertFalse(CrossCloudFederationRoutes.isSupported(CloudType.oci, CloudType.oci));
  }

  /** GCP is one end of every route. */
  @Test
  public void testEveryRouteHasGcpAsAnEnd() {
    for (CloudType source : CloudType.values()) {
      for (CloudType target : CrossCloudFederationRoutes.supportedTargets(source)) {
        assertTrue(source + " -> " + target, source == CloudType.gcp || target == CloudType.gcp);
      }
    }
  }

  @Test
  public void testSources() {
    assertTrue(CrossCloudFederationRoutes.isSource(CloudType.aws));
    assertTrue(CrossCloudFederationRoutes.isSource(CloudType.gcp));
    assertTrue(CrossCloudFederationRoutes.isSource(CloudType.oci));
    // Real hardware, other providers and unknown clouds cannot present a cloud identity.
    assertFalse(CrossCloudFederationRoutes.isSource(CloudType.onprem));
    assertFalse(CrossCloudFederationRoutes.isSource(CloudType.kubernetes));
    assertFalse(CrossCloudFederationRoutes.isSource(CloudType.azu));
    assertFalse(CrossCloudFederationRoutes.isSource(CloudType.unknown));
  }

  @Test
  public void testSupportedTargets() {
    assertEquals(Set.of(CloudType.gcp), CrossCloudFederationRoutes.supportedTargets(CloudType.oci));
    assertEquals(Set.of(CloudType.gcp), CrossCloudFederationRoutes.supportedTargets(CloudType.aws));
    assertEquals(Set.of(CloudType.aws), CrossCloudFederationRoutes.supportedTargets(CloudType.gcp));
    assertEquals(
        Collections.emptySet(), CrossCloudFederationRoutes.supportedTargets(CloudType.onprem));
  }

  @Test
  public void testValidOciIdentityDomainUrl() {
    assertTrue(
        CrossCloudFederationTarget.isValidOciIdentityDomainUrl(
            "https://idcs-abc123.identity.oraclecloud.com"));
    // The OCI console shows the domain URL with the port, so users paste it that way.
    assertTrue(
        CrossCloudFederationTarget.isValidOciIdentityDomainUrl(
            "https://idcs-abc123.identity.oraclecloud.com:443"));
    assertTrue(
        CrossCloudFederationTarget.isValidOciIdentityDomainUrl(
            "https://idcs-abc123.identity.oraclegovcloud.com"));
  }

  @Test
  public void testInvalidOciIdentityDomainUrl() {
    String[] invalid = {
      null,
      "",
      "   ",
      "idcs-abc123.identity.oraclecloud.com",
      "http://idcs-abc123.identity.oraclecloud.com",
      "https://idcs-abc123.identity.oraclecloud.com/",
      "https://idcs-abc123.identity.oraclecloud.com/oauth2/v1/token",
      "https://idcs-abc123.identity.oraclecloud.com:8443",
      "https://user@idcs-abc123.identity.oraclecloud.com",
      "https://idcs-abc123.identity.oraclecloud.com?x=1",
      "https://idcs-abc123.identity.oraclecloud.com evil.example",
      "https://localhost",
      "https://-bad.identity.oraclecloud.com",
      "https://" + "a".repeat(64) + ".oraclecloud.com",
    };
    for (String url : invalid) {
      assertFalse(String.valueOf(url), CrossCloudFederationTarget.isValidOciIdentityDomainUrl(url));
    }
  }
}
