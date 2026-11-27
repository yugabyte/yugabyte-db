// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.models.helpers.CrossCloudFederationDirections.isSource;
import static com.yugabyte.yw.models.helpers.CrossCloudFederationDirections.isSupported;
import static com.yugabyte.yw.models.helpers.CrossCloudFederationDirections.supportedTargets;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import org.junit.Test;

/** The (node cloud -> storage cloud) pairs federation serves, and the rules about OCI's inputs. */
public class CrossCloudFederationDirectionsTest {

  @Test
  public void testSupportedDirections() {
    assertTrue(isSupported(CloudType.aws, CloudType.gcp));
    assertTrue(isSupported(CloudType.gcp, CloudType.aws));
    assertTrue(isSupported(CloudType.oci, CloudType.gcp));
  }

  /**
   * Every pair no flow serves must be refused: a node pointed at one would be configured with
   * credentials that cannot work.
   */
  @Test
  public void testUnsupportedDirections() {
    // Nothing reaches OCI storage yet, and OCI nodes cannot federate to S3.
    assertFalse(isSupported(CloudType.aws, CloudType.oci));
    assertFalse(isSupported(CloudType.gcp, CloudType.oci));
    assertFalse(isSupported(CloudType.oci, CloudType.aws));
    // Same cloud is native, never federated.
    for (CloudType cloud : new CloudType[] {CloudType.aws, CloudType.gcp, CloudType.oci}) {
      assertFalse(cloud.name(), isSupported(cloud, cloud));
    }
  }

  /** GCP is one end of every direction. */
  @Test
  public void testEveryDirectionHasGcpAsAnEnd() {
    for (CloudType source : CloudType.values()) {
      for (CloudType target : supportedTargets(source)) {
        assertTrue(source + " -> " + target, source == CloudType.gcp || target == CloudType.gcp);
      }
    }
  }

  @Test
  public void testSources() {
    for (CloudType cloud : new CloudType[] {CloudType.aws, CloudType.gcp, CloudType.oci}) {
      assertTrue(cloud.name(), isSource(cloud));
    }
    // Real hardware, other providers and unknown clouds cannot present a cloud identity.
    for (CloudType cloud :
        new CloudType[] {
          CloudType.onprem, CloudType.kubernetes, CloudType.azu, CloudType.unknown
        }) {
      assertFalse(cloud.name(), isSource(cloud));
    }
  }

  @Test
  public void testValidOciIdentityDomainUrl() {
    String host = "https://idcs-abc123.identity.oraclecloud.com";
    // The OCI console shows the domain URL with the port, so users paste it that way.
    for (String url : new String[] {host, host + ":443", host.replace("cloud", "govcloud")}) {
      assertTrue(url, CrossCloudFederationTarget.isValidOciIdentityDomainUrl(url));
    }
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
