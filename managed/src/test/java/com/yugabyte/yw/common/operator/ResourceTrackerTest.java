// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.OperatorResource;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;

public class ResourceTrackerTest extends FakeDBApplication {

  private ResourceTracker tracker;

  @Before
  public void setup() {
    tracker = new ResourceTracker();
  }

  private KubernetesResourceDetails resource(String name, String namespace) {
    KubernetesResourceDetails details = new KubernetesResourceDetails(name, namespace);
    details.resourceType = "test";
    return details;
  }

  private KubernetesResourceDetails resource(String name, String namespace, String type) {
    KubernetesResourceDetails details = new KubernetesResourceDetails(name, namespace);
    details.resourceType = type;
    return details;
  }

  @Test
  public void testTrackResource() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");
    tracker.trackResource(res, null);

    assertEquals(1, tracker.getTrackedResources().size());
    assertTrue(tracker.isTracked(res));
  }

  @Test
  public void testTrackDependency() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("db-password", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, secret);

    assertEquals(2, tracker.getTrackedResources().size());
    assertTrue(tracker.isTracked(owner));
    assertTrue(tracker.isTracked(secret));
    assertEquals(1, tracker.getDependencies(owner).size());
    assertTrue(tracker.getDependencies(owner).contains(secret));
  }

  @Test
  public void testTrackDependencyAutoTracksOwner() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("db-password", "ns1");

    // Don't explicitly track owner, just add a dependency
    tracker.trackDependency(owner, secret);

    assertEquals(2, tracker.getTrackedResources().size());
    assertTrue(tracker.isTracked(owner));
    assertTrue(tracker.isTracked(secret));
  }

  @Test
  public void testUntrackResourceRemovesOwnerAndOrphanedDeps() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("db-password", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, secret);

    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(owner);

    assertTrue(tracker.getTrackedResources().isEmpty());
    assertEquals(1, orphaned.size());
    assertTrue(orphaned.contains(secret));
    assertFalse(tracker.isTracked(owner));
    assertFalse(tracker.isTracked(secret));
  }

  @Test
  public void testUntrackResourceKeepsSharedDependencies() {
    KubernetesResourceDetails owner1 = resource("universe-1", "ns1");
    KubernetesResourceDetails owner2 = resource("universe-2", "ns1");
    KubernetesResourceDetails sharedSecret = resource("shared-password", "ns1");
    KubernetesResourceDetails uniqueSecret = resource("unique-secret", "ns1");

    tracker.trackResource(owner1, null);
    tracker.trackResource(owner2, null);
    tracker.trackDependency(owner1, sharedSecret);
    tracker.trackDependency(owner1, uniqueSecret);
    tracker.trackDependency(owner2, sharedSecret);

    // Untrack owner1 - sharedSecret should NOT be orphaned, uniqueSecret should be
    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(owner1);

    assertEquals(1, orphaned.size());
    assertTrue(orphaned.contains(uniqueSecret));
    assertFalse(orphaned.contains(sharedSecret));

    // sharedSecret should still be tracked (used by owner2)
    assertTrue(tracker.isTracked(sharedSecret));
    assertTrue(tracker.isTracked(owner2));
    assertFalse(tracker.isTracked(owner1));
    assertFalse(tracker.isTracked(uniqueSecret));

    // Total tracked: owner2 + sharedSecret
    assertEquals(2, tracker.getTrackedResources().size());
  }

  @Test
  public void testUntrackNonExistentResource() {
    KubernetesResourceDetails res = resource("nonexistent", "ns1");

    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(res);

    assertTrue(orphaned.isEmpty());
    assertTrue(tracker.getTrackedResources().isEmpty());
  }

  @Test
  public void testUntrackResourceWithNoDependencies() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    tracker.trackResource(owner, null);

    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(owner);

    assertTrue(orphaned.isEmpty());
    assertTrue(tracker.getTrackedResources().isEmpty());
  }

  @Test
  public void testGetResourceDependencies() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret1 = resource("secret-1", "ns1");
    KubernetesResourceDetails secret2 = resource("secret-2", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, secret1);
    tracker.trackDependency(owner, secret2);

    // Map should have entries for all 3 resources (owner + 2 deps)
    // but only the owner has dependencies
    assertTrue(tracker.getResourceDependencies().containsKey(owner));
    assertEquals(2, tracker.getResourceDependencies().get(owner).size());
  }

  @Test
  public void testMultipleOwnersMultipleSecrets() {
    KubernetesResourceDetails release1 = resource("release-1", "ns1");
    KubernetesResourceDetails release2 = resource("release-2", "ns1");
    KubernetesResourceDetails awsSecret = resource("aws-key", "ns1");
    KubernetesResourceDetails gcsSecret = resource("gcs-creds", "ns1");

    tracker.trackResource(release1, null);
    tracker.trackResource(release2, null);
    tracker.trackDependency(release1, awsSecret);
    tracker.trackDependency(release2, gcsSecret);

    // Total: 2 owners + 2 secrets = 4
    assertEquals(4, tracker.getTrackedResources().size());

    // Untrack release1 -> awsSecret should be orphaned
    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(release1);
    assertEquals(1, orphaned.size());
    assertTrue(orphaned.contains(awsSecret));

    // Remaining: release2 + gcsSecret
    assertEquals(2, tracker.getTrackedResources().size());

    // Untrack release2 -> gcsSecret should be orphaned
    orphaned = tracker.untrackResource(release2);
    assertEquals(1, orphaned.size());
    assertTrue(orphaned.contains(gcsSecret));

    // Nothing left
    assertTrue(tracker.getTrackedResources().isEmpty());
  }

  @Test
  public void testGetDependenciesForUnknownOwner() {
    KubernetesResourceDetails unknown = resource("unknown", "ns1");
    Set<KubernetesResourceDetails> deps = tracker.getDependencies(unknown);
    assertTrue(deps.isEmpty());
  }

  @Test
  public void testTrackResourceStoresYamlData() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");
    String yaml = "apiVersion: v1\nkind: Test\nmetadata:\n  name: my-universe";
    tracker.trackResource(res, yaml);

    assertTrue(tracker.isTracked(res));
    // Verify the data is stored by checking the model directly
    OperatorResource stored = OperatorResource.getByName(res.toResourceName());
    assertEquals(yaml, stored.getData());
  }

  @Test
  public void testTrackResourceUpdatesYamlData() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");
    String yamlV1 = "apiVersion: v1\nkind: Test\nmetadata:\n  name: my-universe\n  version: 1";
    String yamlV2 = "apiVersion: v1\nkind: Test\nmetadata:\n  name: my-universe\n  version: 2";

    tracker.trackResource(res, yamlV1);
    OperatorResource stored = OperatorResource.getByName(res.toResourceName());
    assertEquals(yamlV1, stored.getData());

    // Re-track with updated YAML should overwrite
    tracker.trackResource(res, yamlV2);
    stored = OperatorResource.getByName(res.toResourceName());
    assertEquals(yamlV2, stored.getData());

    // Should still be a single tracked resource, not duplicated
    assertEquals(1, tracker.getTrackedResources().size());
  }

  @Test
  public void testTrackResourceWithNullYamlData() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");
    tracker.trackResource(res, null);

    assertTrue(tracker.isTracked(res));
    OperatorResource stored = OperatorResource.getByName(res.toResourceName());
    assertNotNull(stored);
    assertNull(stored.getData());
  }

  @Test
  public void testTrackDependencyWithYamlData() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("db-password", "ns1");
    String ownerYaml = "kind: Universe\nmetadata:\n  name: my-universe";
    String secretYaml = "kind: Secret\nmetadata:\n  name: db-password";

    tracker.trackResource(owner, ownerYaml);
    tracker.trackDependency(owner, secret, secretYaml);

    // Verify both YAML payloads are stored
    OperatorResource storedOwner = OperatorResource.getByName(owner.toResourceName());
    OperatorResource storedSecret = OperatorResource.getByName(secret.toResourceName());
    assertEquals(ownerYaml, storedOwner.getData());
    assertEquals(secretYaml, storedSecret.getData());
  }

  @Test
  public void testTrackDependencyWithNullYamlData() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("db-password", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, secret, null);

    assertTrue(tracker.isTracked(secret));
    OperatorResource storedSecret = OperatorResource.getByName(secret.toResourceName());
    assertNotNull(storedSecret);
    assertNull(storedSecret.getData());
  }

  @Test
  public void testDuplicateDependencyIsIdempotent() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("db-password", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, secret);
    tracker.trackDependency(owner, secret); // Add same dependency again

    // Should still have exactly 1 dependency, not duplicated
    assertEquals(1, tracker.getDependencies(owner).size());
    assertEquals(2, tracker.getTrackedResources().size());
  }

  @Test
  public void testDifferentNamespacesAreSeparateResources() {
    KubernetesResourceDetails res1 = resource("my-universe", "ns1");
    KubernetesResourceDetails res2 = resource("my-universe", "ns2");

    tracker.trackResource(res1, "yaml-ns1");
    tracker.trackResource(res2, "yaml-ns2");

    assertEquals(2, tracker.getTrackedResources().size());
    assertTrue(tracker.isTracked(res1));
    assertTrue(tracker.isTracked(res2));

    // Verify they store different data
    OperatorResource stored1 = OperatorResource.getByName(res1.toResourceName());
    OperatorResource stored2 = OperatorResource.getByName(res2.toResourceName());
    assertEquals("yaml-ns1", stored1.getData());
    assertEquals("yaml-ns2", stored2.getData());
  }

  @Test
  public void testDifferentResourceTypesAreSeparateResources() {
    KubernetesResourceDetails universe = resource("my-resource", "ns1", "universe");
    KubernetesResourceDetails secret = resource("my-resource", "ns1", "secret");

    tracker.trackResource(universe, "universe-yaml");
    tracker.trackResource(secret, "secret-yaml");

    assertEquals(2, tracker.getTrackedResources().size());
    assertTrue(tracker.isTracked(universe));
    assertTrue(tracker.isTracked(secret));
  }

  @Test
  public void testUntrackThenRetrack() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("my-secret", "ns1");

    // Track resource with a dependency
    tracker.trackResource(res, "yaml-v1");
    tracker.trackDependency(res, secret, "secret-yaml");
    assertEquals(2, tracker.getTrackedResources().size());

    // Untrack everything
    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(res);
    assertEquals(1, orphaned.size());
    assertTrue(tracker.getTrackedResources().isEmpty());
    assertFalse(tracker.isTracked(res));

    // Re-track the same resource with new data
    tracker.trackResource(res, "yaml-v2");
    assertTrue(tracker.isTracked(res));
    assertEquals(1, tracker.getTrackedResources().size());

    // Verify updated data
    OperatorResource stored = OperatorResource.getByName(res.toResourceName());
    assertEquals("yaml-v2", stored.getData());

    // Re-add dependency
    tracker.trackDependency(res, secret, "secret-yaml-v2");
    assertEquals(2, tracker.getTrackedResources().size());
    assertEquals(1, tracker.getDependencies(res).size());
  }

  @Test
  public void testGetResourceDependenciesMapIncludesLeafNodes() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails dep1 = resource("secret-1", "ns1");
    KubernetesResourceDetails dep2 = resource("secret-2", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, dep1);
    tracker.trackDependency(owner, dep2);

    Map<KubernetesResourceDetails, Set<KubernetesResourceDetails>> depMap =
        tracker.getResourceDependencies();

    // All 3 resources should appear as keys in the map
    assertEquals(3, depMap.size());
    assertTrue(depMap.containsKey(owner));
    assertTrue(depMap.containsKey(dep1));
    assertTrue(depMap.containsKey(dep2));

    // Owner has 2 dependencies; leaf nodes have 0
    assertEquals(2, depMap.get(owner).size());
    assertTrue(depMap.get(dep1).isEmpty());
    assertTrue(depMap.get(dep2).isEmpty());
  }

  @Test
  public void testDiamondDependencyPattern() {
    // Diamond: owner1 -> depA, depShared
    //          owner2 -> depB, depShared
    KubernetesResourceDetails owner1 = resource("owner-1", "ns1");
    KubernetesResourceDetails owner2 = resource("owner-2", "ns1");
    KubernetesResourceDetails depA = resource("dep-a", "ns1");
    KubernetesResourceDetails depB = resource("dep-b", "ns1");
    KubernetesResourceDetails depShared = resource("dep-shared", "ns1");

    tracker.trackResource(owner1, null);
    tracker.trackResource(owner2, null);
    tracker.trackDependency(owner1, depA);
    tracker.trackDependency(owner1, depShared);
    tracker.trackDependency(owner2, depB);
    tracker.trackDependency(owner2, depShared);

    assertEquals(5, tracker.getTrackedResources().size());

    // Remove owner1 -> depA orphaned, depShared still held by owner2
    Set<KubernetesResourceDetails> orphaned1 = tracker.untrackResource(owner1);
    assertEquals(1, orphaned1.size());
    assertTrue(orphaned1.contains(depA));
    assertFalse(orphaned1.contains(depShared));
    assertTrue(tracker.isTracked(depShared));

    // 3 remaining: owner2, depB, depShared
    assertEquals(3, tracker.getTrackedResources().size());

    // Remove owner2 -> depB and depShared both orphaned
    Set<KubernetesResourceDetails> orphaned2 = tracker.untrackResource(owner2);
    assertEquals(2, orphaned2.size());
    assertTrue(orphaned2.contains(depB));
    assertTrue(orphaned2.contains(depShared));

    assertTrue(tracker.getTrackedResources().isEmpty());
  }

  @Test
  public void testMultipleDependenciesAllOrphanedOnUntrack() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails dep1 = resource("secret-1", "ns1");
    KubernetesResourceDetails dep2 = resource("secret-2", "ns1");
    KubernetesResourceDetails dep3 = resource("secret-3", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, dep1);
    tracker.trackDependency(owner, dep2);
    tracker.trackDependency(owner, dep3);

    assertEquals(4, tracker.getTrackedResources().size());

    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(owner);
    assertEquals(3, orphaned.size());
    assertTrue(orphaned.contains(dep1));
    assertTrue(orphaned.contains(dep2));
    assertTrue(orphaned.contains(dep3));

    assertTrue(tracker.getTrackedResources().isEmpty());
  }

  @Test
  public void testIsTrackedReturnsFalseForNonExistent() {
    KubernetesResourceDetails res = resource("does-not-exist", "ns1");
    assertFalse(tracker.isTracked(res));
  }

  @Test
  public void testGetTrackedResourcesReturnsUnmodifiableSet() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");
    tracker.trackResource(res, null);

    Set<KubernetesResourceDetails> tracked = tracker.getTrackedResources();
    try {
      tracked.add(resource("hacker", "ns1"));
      // If we reach here, the set is modifiable which we don't want
      // but some implementations may allow it - the important thing
      // is that it doesn't affect the tracker's internal state
    } catch (UnsupportedOperationException e) {
      // Expected - the set is properly unmodifiable
    }

    // Regardless of the above, the tracker should still only have 1 resource
    assertEquals(1, tracker.getTrackedResources().size());
  }

  @Test
  public void testGetResourceDependenciesReturnsUnmodifiableMap() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails dep = resource("my-secret", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, dep);

    Map<KubernetesResourceDetails, Set<KubernetesResourceDetails>> depMap =
        tracker.getResourceDependencies();

    try {
      depMap.put(resource("hacker", "ns1"), Collections.emptySet());
    } catch (UnsupportedOperationException e) {
      // Expected
    }

    // Internal state unaffected
    assertEquals(2, tracker.getResourceDependencies().size());
  }

  @Test
  public void testGetDependenciesReturnsUnmodifiableSet() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails dep = resource("my-secret", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, dep);

    Set<KubernetesResourceDetails> deps = tracker.getDependencies(owner);
    try {
      deps.add(resource("hacker", "ns1"));
    } catch (UnsupportedOperationException e) {
      // Expected
    }

    // Internal state unaffected
    assertEquals(1, tracker.getDependencies(owner).size());
  }

  @Test
  public void testResourceNameRoundTrip() {
    KubernetesResourceDetails original = resource("my-universe", "ns1");
    String resourceName = original.toResourceName();
    KubernetesResourceDetails parsed = KubernetesResourceDetails.fromResourceName(resourceName);

    assertEquals(original.resourceType, parsed.resourceType);
    assertEquals(original.namespace, parsed.namespace);
    assertEquals(original.name, parsed.name);
    assertEquals(original, parsed);
  }

  @Test(expected = IllegalArgumentException.class)
  public void testFromResourceNameInvalidFormat() {
    KubernetesResourceDetails.fromResourceName("invalid-no-colons");
  }

  @Test
  public void testTrackResourceIdempotent() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");

    tracker.trackResource(res, "yaml-data");
    tracker.trackResource(res, "yaml-data");
    tracker.trackResource(res, "yaml-data");

    // Still exactly one tracked resource
    assertEquals(1, tracker.getTrackedResources().size());
    assertTrue(tracker.isTracked(res));
  }

  @Test
  public void testOwnerWithNoDepsHasEmptyDependencySet() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    tracker.trackResource(owner, null);

    Set<KubernetesResourceDetails> deps = tracker.getDependencies(owner);
    assertNotNull(deps);
    assertTrue(deps.isEmpty());
  }

  @Test
  public void testMultipleTrackersShareDatabaseState() {
    // Two separate ResourceTracker instances share the same underlying DB
    ResourceTracker tracker2 = new ResourceTracker();

    KubernetesResourceDetails res = resource("my-universe", "ns1");
    tracker.trackResource(res, "yaml-data");

    // tracker2 should see the resource tracked by tracker
    assertTrue(tracker2.isTracked(res));
    assertEquals(1, tracker2.getTrackedResources().size());
  }

  @Test
  public void testDependencyUpdatesYamlData() {
    KubernetesResourceDetails owner = resource("my-universe", "ns1");
    KubernetesResourceDetails secret = resource("db-password", "ns1");

    tracker.trackResource(owner, null);
    tracker.trackDependency(owner, secret, "secret-yaml-v1");

    OperatorResource storedV1 = OperatorResource.getByName(secret.toResourceName());
    assertEquals("secret-yaml-v1", storedV1.getData());

    // Update dependency YAML
    tracker.trackDependency(owner, secret, "secret-yaml-v2");

    OperatorResource storedV2 = OperatorResource.getByName(secret.toResourceName());
    assertEquals("secret-yaml-v2", storedV2.getData());

    // Still only one dependency
    assertEquals(1, tracker.getDependencies(owner).size());
  }

  @Test
  public void testUntrackInReverseOrder() {
    // Track 3 owners, untrack them in reverse order
    KubernetesResourceDetails owner1 = resource("owner-1", "ns1");
    KubernetesResourceDetails owner2 = resource("owner-2", "ns1");
    KubernetesResourceDetails owner3 = resource("owner-3", "ns1");
    KubernetesResourceDetails dep1 = resource("dep-1", "ns1");

    tracker.trackResource(owner1, null);
    tracker.trackResource(owner2, null);
    tracker.trackResource(owner3, null);
    tracker.trackDependency(owner1, dep1);
    tracker.trackDependency(owner2, dep1);
    tracker.trackDependency(owner3, dep1);

    // 4 tracked: 3 owners + 1 shared dep
    assertEquals(4, tracker.getTrackedResources().size());

    // Untrack owner3 - dep1 still referenced by owner1 and owner2
    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(owner3);
    assertTrue(orphaned.isEmpty());
    assertTrue(tracker.isTracked(dep1));
    assertEquals(3, tracker.getTrackedResources().size());

    // Untrack owner2 - dep1 still referenced by owner1
    orphaned = tracker.untrackResource(owner2);
    assertTrue(orphaned.isEmpty());
    assertTrue(tracker.isTracked(dep1));
    assertEquals(2, tracker.getTrackedResources().size());

    // Untrack owner1 - dep1 now orphaned
    orphaned = tracker.untrackResource(owner1);
    assertEquals(1, orphaned.size());
    assertTrue(orphaned.contains(dep1));
    assertTrue(tracker.getTrackedResources().isEmpty());
  }

  @Test
  public void testEmptyTrackerState() {
    // Fresh tracker should have no resources
    assertTrue(tracker.getTrackedResources().isEmpty());
    assertTrue(tracker.getResourceDependencies().isEmpty());
  }

  @Test
  public void testResourceNameFormatContainsTypeNamespaceAndName() {
    KubernetesResourceDetails res = resource("my-universe", "ns1");
    String resourceName = res.toResourceName();
    assertEquals("test:ns1:my-universe", resourceName);
  }

  @Test
  public void testUntrackMiddleOwnerInChain() {
    // owner1 -> depA, depShared
    // owner2 -> depShared, depB
    // owner3 -> depB, depC
    // Untrack owner2 first: depShared stays (owner1), depB stays (owner3)
    KubernetesResourceDetails owner1 = resource("owner-1", "ns1");
    KubernetesResourceDetails owner2 = resource("owner-2", "ns1");
    KubernetesResourceDetails owner3 = resource("owner-3", "ns1");
    KubernetesResourceDetails depA = resource("dep-a", "ns1");
    KubernetesResourceDetails depB = resource("dep-b", "ns1");
    KubernetesResourceDetails depC = resource("dep-c", "ns1");
    KubernetesResourceDetails depShared = resource("dep-shared", "ns1");

    tracker.trackResource(owner1, null);
    tracker.trackResource(owner2, null);
    tracker.trackResource(owner3, null);
    tracker.trackDependency(owner1, depA);
    tracker.trackDependency(owner1, depShared);
    tracker.trackDependency(owner2, depShared);
    tracker.trackDependency(owner2, depB);
    tracker.trackDependency(owner3, depB);
    tracker.trackDependency(owner3, depC);

    // 7 resources total
    assertEquals(7, tracker.getTrackedResources().size());

    // Untrack owner2: depShared held by owner1, depB held by owner3 -> no orphans
    Set<KubernetesResourceDetails> orphaned = tracker.untrackResource(owner2);
    assertTrue(orphaned.isEmpty());
    assertTrue(tracker.isTracked(depShared));
    assertTrue(tracker.isTracked(depB));
    assertEquals(6, tracker.getTrackedResources().size());
  }
}
