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
 * already reaches the cloud natively, skip because it is not a cloud VM, skip because no route
 * exists, and fail because the node's cloud is unknown.
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

  private static CrossCloudFederationTarget select(
      CloudType source, CrossCloudFederationTarget... targets) {
    return ManageCloudFederation.selectTarget("node-1", source, Arrays.asList(targets));
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

  /** The new route: a node on OCI reaches GCS. */
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

  // --- skip: no route ---

  /** OCI has no route to S3, so a node there is left alone rather than given credentials. */
  @Test
  public void testOciNodeHasNoRouteToS3() {
    assertNull(select(CloudType.oci, s3()));
  }

  /** The reachable target is still configured when an unreachable one sits beside it. */
  @Test
  public void testUnreachableTargetIsDroppedNotChosen() {
    assertEquals(CloudType.gcp, select(CloudType.oci, s3(), gcs()).targetCloud);
  }

  /** Nothing reaches OCI storage yet: it must be neither chosen nor allowed to fail the node. */
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
    assertNull(ManageCloudFederation.selectTarget("node-1", CloudType.aws, null));
    assertNull(
        ManageCloudFederation.selectTarget("node-1", CloudType.aws, Collections.emptyList()));
  }

  @Test
  public void testIncompleteAndNullEntriesAreIgnored() {
    // An aws entry with no role to assume is unusable, so it is not a configured target.
    CrossCloudFederationTarget noRole =
        CrossCloudFederationTarget.of(CloudType.aws, AUDIENCE, null);
    assertNull(select(CloudType.gcp, noRole));
    assertNull(ManageCloudFederation.selectTarget("n", CloudType.gcp, Arrays.asList(null, noRole)));
    assertEquals(
        CloudType.gcp,
        ManageCloudFederation.selectTarget("n", CloudType.aws, Arrays.asList(null, gcs()))
            .targetCloud);
  }

  // --- fail ---

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
            () -> ManageCloudFederation.selectTarget("node-1", CloudType.aws, twice));
    assertTrue(e.getMessage(), e.getMessage().contains("more than one storage cloud"));
  }
}
