// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import org.junit.Test;

/**
 * Which target a node gets, as a function of the cloud it runs on and what its provider configures.
 * Every outcome is a deliberate decision, so each one is pinned: configure, skip because the node
 * already reaches the cloud natively, skip because it is not a cloud VM, skip because the direction
 * is unsupported, and fail because the node's cloud is unknown.
 */
public class ManageCloudFederationSelectTargetTest {

  private static final String AUDIENCE = "//iam.googleapis.com/projects/1/providers/a";
  private static final String ROLE_ARN = "arn:aws:iam::123456789012:role/yb-s3-fed-role";

  private static CrossCloudFederationTarget gcs() {
    return CrossCloudFederationTarget.of(CloudType.gcp, AUDIENCE, null);
  }

  private static CrossCloudFederationTarget s3() {
    return CrossCloudFederationTarget.of(CloudType.aws, AUDIENCE, ROLE_ARN);
  }

  private static CrossCloudFederationTarget oci() {
    return CrossCloudFederationTarget.of(CloudType.oci, AUDIENCE, null);
  }

  /** A node whose cloud was detected from its metadata service, as on an on-prem provider. */
  private static CrossCloudFederationTarget select(
      CloudType source, CrossCloudFederationTarget... targets) {
    return ManageCloudFederation.selectTarget("node-1", source, false, Arrays.asList(targets));
  }

  /** A node whose cloud is named by its provider, as on an AWS, GCP or OCI provider. */
  private static CrossCloudFederationTarget selectFixed(
      CloudType source, CrossCloudFederationTarget... targets) {
    return ManageCloudFederation.selectTarget("node-1", source, true, Arrays.asList(targets));
  }

  // --- configure ---

  @Test
  public void testAwsNodeReachesGcs() {
    assertEquals(CloudType.gcp, select(CloudType.aws, gcs()).targetCloud);
  }

  @Test
  public void testGcpNodeReachesS3() {
    assertEquals(CloudType.aws, select(CloudType.gcp, s3()).targetCloud);
  }

  /** The new direction: a node on OCI reaches GCS. */
  @Test
  public void testOciNodeReachesGcs() {
    assertEquals(CloudType.gcp, select(CloudType.oci, gcs()).targetCloud);
  }

  /** One provider, two clouds: each node takes the entry for the cloud it is not on. */
  @Test
  public void testMixedProviderGivesEachNodeItsOwnTarget() {
    assertEquals(CloudType.gcp, select(CloudType.aws, gcs(), s3()).targetCloud);
    assertEquals(CloudType.aws, select(CloudType.gcp, gcs(), s3()).targetCloud);
    assertEquals(CloudType.gcp, select(CloudType.oci, gcs(), s3()).targetCloud);
  }

  // --- skip: native ---

  @Test
  public void testNodeOnTheTargetCloudIsLeftNative() {
    assertNull(select(CloudType.aws, s3()));
    assertNull(select(CloudType.gcp, gcs()));
  }

  // --- skip: not a cloud VM ---

  @Test
  public void testBareMetalAndOtherCloudsAreSkipped() {
    assertNull(select(CloudType.onprem, gcs(), s3()));
    assertNull(select(CloudType.kubernetes, gcs(), s3()));
    assertNull(select(CloudType.azu, gcs(), s3()));
  }

  // --- skip: unsupported direction, cloud detected from the node ---

  /**
   * An on-prem provider is shared by nodes of any cloud, so a target one of them cannot federate to
   * is left out rather than refused.
   */
  @Test
  public void testDetectedOciNodeCannotFederateToS3() {
    assertNull(select(CloudType.oci, s3()));
  }

  /** The reachable target is still configured when an unreachable one sits beside it. */
  @Test
  public void testUnreachableTargetIsDroppedNotChosen() {
    assertEquals(CloudType.gcp, select(CloudType.oci, s3(), gcs()).targetCloud);
  }

  /** Nothing reaches OCI storage yet, so a detected node never picks it. */
  @Test
  public void testOciTargetIsNotReachableFromAnywhere() {
    assertNull(select(CloudType.aws, oci()));
    assertNull(select(CloudType.gcp, oci()));
    assertEquals(CloudType.gcp, select(CloudType.aws, oci(), gcs()).targetCloud);
    assertEquals(CloudType.aws, select(CloudType.gcp, oci(), s3()).targetCloud);
  }

  // --- skip: nothing configured ---

  @Test
  public void testNoTargetsMeansNothingToDo() {
    assertNull(ManageCloudFederation.selectTarget("node-1", CloudType.aws, false, null));
    assertNull(
        ManageCloudFederation.selectTarget(
            "node-1", CloudType.aws, false, Collections.emptyList()));
  }

  @Test
  public void testIncompleteAndNullEntriesAreIgnored() {
    // An aws entry with no role to assume is unusable, so it is not a configured target.
    CrossCloudFederationTarget noRole =
        CrossCloudFederationTarget.of(CloudType.aws, AUDIENCE, null);
    assertNull(select(CloudType.gcp, noRole));
    assertNull(
        ManageCloudFederation.selectTarget("n", CloudType.gcp, false, Arrays.asList(null, noRole)));
    assertEquals(
        CloudType.gcp,
        ManageCloudFederation.selectTarget("n", CloudType.aws, false, Arrays.asList(null, gcs()))
            .targetCloud);
  }

  // --- fail ---

  /**
   * A provider that names the node's cloud has every node there, so an unsupported target can never
   * be served. Validation refuses it, but can be bypassed; the node must not be left silently
   * without credentials for a bucket its provider says it reaches. Everything else is unchanged.
   */
  @Test
  public void testProviderFixedSourceFailsOnAnUnsupportedTarget() {
    IllegalStateException e =
        assertThrows(IllegalStateException.class, () -> selectFixed(CloudType.oci, s3()));
    assertTrue(e.getMessage(), e.getMessage().contains("cannot federate to [aws]"));
    assertThrows(IllegalStateException.class, () -> selectFixed(CloudType.oci, s3(), gcs()));
    assertThrows(IllegalStateException.class, () -> selectFixed(CloudType.aws, oci(), gcs()));

    assertEquals(CloudType.gcp, selectFixed(CloudType.oci, gcs()).targetCloud);
    assertNull(selectFixed(CloudType.aws, s3()));
    assertNull(selectFixed(CloudType.oci));
  }

  /** A guess would write credentials that cannot work and hide a node that should be federated. */
  @Test
  public void testUnknownCloudFailsWithTheReason() {
    IllegalStateException e =
        assertThrows(IllegalStateException.class, () -> select(null, gcs(), s3()));
    assertTrue(e.getMessage(), e.getMessage().contains("node-1"));
    assertTrue(e.getMessage(), e.getMessage().contains("could not be determined"));
  }

  /**
   * Provider validation refuses two entries for one cloud, but data from before that check may hold
   * them. Picking either would be arbitrary, so the node fails loudly.
   */
  @Test
  public void testDuplicateTargetsFailRatherThanPickOne() {
    List<CrossCloudFederationTarget> twice = Arrays.asList(gcs(), gcs());
    IllegalStateException e =
        assertThrows(
            IllegalStateException.class,
            () -> ManageCloudFederation.selectTarget("node-1", CloudType.aws, false, twice));
    // One cloud twice, so the message must not claim that several clouds were configured.
    assertTrue(e.getMessage(), e.getMessage().contains("more than one federated target"));
    assertTrue(e.getMessage(), e.getMessage().contains("gcp, gcp"));
  }

  // --- the gate ---

  /**
   * While OCI is not enabled as a source, a node on OCI is left alone rather than failed, so
   * turning the gate off does not break operations on a universe that already has OCI federation.
   */
  @Test
  public void testOciNodeIsSkippedNotFailedWhileTheGateIsOff() {
    assertNull(ManageCloudFederation.ociSourceSkipReason(CloudType.oci, true));

    String gateOff = ManageCloudFederation.ociSourceSkipReason(CloudType.oci, false);
    assertTrue(gateOff, gateOff.contains("yb.cross_cloud_federation.enable_oci_source"));
  }

  /** Only OCI is held back: every other node is judged by the direction rules alone. */
  @Test
  public void testOtherSourcesAreNeverHeldBackByTheOciGate() {
    for (CloudType cloud :
        new CloudType[] {CloudType.aws, CloudType.gcp, CloudType.onprem, CloudType.kubernetes}) {
      assertNull(cloud.name(), ManageCloudFederation.ociSourceSkipReason(cloud, false));
    }
    assertNull(ManageCloudFederation.ociSourceSkipReason(null, false));
  }
}
