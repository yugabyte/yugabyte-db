// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

/** Unit tests for the FileCollection model */
public class FileCollectionTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe defaultUniverse;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
  }

  @Test
  public void testCreate_Success() {
    Map<String, String> nodeTarPaths = new HashMap<>();
    nodeTarPaths.put("node1", "/tmp/collected-files-node1.tar.gz");
    nodeTarPaths.put("node2", "/tmp/collected-files-node2.tar.gz");

    Map<String, String> nodeAddresses = new HashMap<>();
    nodeAddresses.put("node1", "10.0.0.1");
    nodeAddresses.put("node2", "10.0.0.2");

    FileCollection fc =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    assertNotNull(fc);
    assertNotNull(fc.getCollectionUuid());
    assertEquals(defaultCustomer.getUuid(), fc.getCustomerUuid());
    assertEquals(defaultUniverse.getUniverseUUID(), fc.getUniverseUuid());
    assertEquals(FileCollection.Status.COLLECTED, fc.getStatus());
    assertNotNull(fc.getCreatedAt());
    assertEquals(nodeTarPaths, fc.getNodeTarPaths());
    assertEquals(nodeAddresses, fc.getNodeAddresses());
  }

  @Test
  public void testGet_Success() {
    Map<String, String> nodeTarPaths = Map.of("node1", "/tmp/test.tar.gz");
    Map<String, String> nodeAddresses = Map.of("node1", "10.0.0.1");

    FileCollection created =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    Optional<FileCollection> retrieved = FileCollection.maybeGet(created.getCollectionUuid());

    assertTrue(retrieved.isPresent());
    assertEquals(created.getCollectionUuid(), retrieved.get().getCollectionUuid());
  }

  @Test
  public void testGet_NotFound() {
    Optional<FileCollection> result = FileCollection.maybeGet(UUID.randomUUID());
    assertFalse(result.isPresent());
  }

  @Test
  public void testGetOrBadRequest_Success() {
    Map<String, String> nodeTarPaths = Map.of("node1", "/tmp/test.tar.gz");
    Map<String, String> nodeAddresses = Map.of("node1", "10.0.0.1");

    FileCollection created =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    FileCollection retrieved = FileCollection.getOrBadRequest(created.getCollectionUuid());

    assertNotNull(retrieved);
    assertEquals(created.getCollectionUuid(), retrieved.getCollectionUuid());
  }

  @Test
  public void testGetOrBadRequest_NotFound() {
    UUID nonExistentUuid = UUID.randomUUID();

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> FileCollection.getOrBadRequest(nonExistentUuid));

    assertEquals(400, exception.getHttpStatus());
    assertTrue(exception.getMessage().contains("not found"));
  }

  @Test
  public void testStatusTransitions() {
    Map<String, String> nodeTarPaths = Map.of("node1", "/tmp/test.tar.gz");
    Map<String, String> nodeAddresses = Map.of("node1", "10.0.0.1");

    FileCollection fc =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    // Initial status should be COLLECTED
    assertEquals(FileCollection.Status.COLLECTED, fc.getStatus());
    assertFalse(fc.isCleanedUp());
    assertFalse(fc.isDbNodesCleaned());
    assertFalse(fc.isYbaCleaned());

    // Mark as downloading
    fc.markDownloading();
    assertEquals(FileCollection.Status.DOWNLOADING, fc.getStatus());

    // Mark as downloaded
    fc.markDownloaded("/opt/yugaware/collected-files/test");
    assertEquals(FileCollection.Status.DOWNLOADED, fc.getStatus());
    assertNotNull(fc.getDownloadedAt());
    assertEquals("/opt/yugaware/collected-files/test", fc.getOutputPath());

    // Mark DB nodes cleaned
    fc.markDbNodesCleaned();
    assertEquals(FileCollection.Status.DB_NODES_CLEANED, fc.getStatus());
    assertTrue(fc.isDbNodesCleaned());
    assertFalse(fc.isYbaCleaned());
    assertFalse(fc.isCleanedUp());

    // Mark YBA cleaned (should transition to CLEANED_UP since DB nodes already cleaned)
    fc.markYbaCleaned();
    assertEquals(FileCollection.Status.CLEANED_UP, fc.getStatus());
    assertTrue(fc.isCleanedUp());
  }

  @Test
  public void testMarkYbaCleanedFirst() {
    Map<String, String> nodeTarPaths = Map.of("node1", "/tmp/test.tar.gz");
    Map<String, String> nodeAddresses = Map.of("node1", "10.0.0.1");

    FileCollection fc =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    // Mark downloaded first
    fc.markDownloading();
    fc.markDownloaded("/opt/yugaware/collected-files/test");

    // Mark YBA cleaned first
    fc.markYbaCleaned();
    assertEquals(FileCollection.Status.YBA_CLEANED, fc.getStatus());
    assertTrue(fc.isYbaCleaned());
    assertFalse(fc.isDbNodesCleaned());
    assertFalse(fc.isCleanedUp());

    // Now mark DB nodes cleaned (should transition to CLEANED_UP)
    fc.markDbNodesCleaned();
    assertEquals(FileCollection.Status.CLEANED_UP, fc.getStatus());
    assertTrue(fc.isCleanedUp());
  }

  @Test
  public void testMarkCleanedUp() {
    Map<String, String> nodeTarPaths = Map.of("node1", "/tmp/test.tar.gz");
    Map<String, String> nodeAddresses = Map.of("node1", "10.0.0.1");

    FileCollection fc =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    // Direct cleanup
    fc.markCleanedUp();
    assertEquals(FileCollection.Status.CLEANED_UP, fc.getStatus());
    assertTrue(fc.isCleanedUp());
  }

  @Test
  public void testMarkFailed() {
    Map<String, String> nodeTarPaths = Map.of("node1", "/tmp/test.tar.gz");
    Map<String, String> nodeAddresses = Map.of("node1", "10.0.0.1");

    FileCollection fc =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    fc.markFailed();
    assertEquals(FileCollection.Status.FAILED, fc.getStatus());
  }

  @Test
  public void testGetActiveByUniverse() {
    Map<String, String> nodeTarPaths = Map.of("node1", "/tmp/test.tar.gz");
    Map<String, String> nodeAddresses = Map.of("node1", "10.0.0.1");

    // Create a few collections
    FileCollection fc1 =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    FileCollection fc2 =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    FileCollection fc3 =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    // Mark one as cleaned up
    fc3.markCleanedUp();

    // Get active collections
    List<FileCollection> activeCollections =
        FileCollection.getActiveByUniverse(defaultUniverse.getUniverseUUID());

    // Should have 2 active (not cleaned up)
    assertEquals(2, activeCollections.size());
    assertTrue(
        activeCollections.stream()
            .anyMatch(fc -> fc.getCollectionUuid().equals(fc1.getCollectionUuid())));
    assertTrue(
        activeCollections.stream()
            .anyMatch(fc -> fc.getCollectionUuid().equals(fc2.getCollectionUuid())));
    assertFalse(
        activeCollections.stream()
            .anyMatch(fc -> fc.getCollectionUuid().equals(fc3.getCollectionUuid())));
  }

  @Test
  public void testNodeTarPathsSerialization() {
    Map<String, String> nodeTarPaths = new HashMap<>();
    nodeTarPaths.put("node-with-special-chars", "/tmp/path with spaces.tar.gz");
    nodeTarPaths.put("node2", "/tmp/normal-path.tar.gz");

    Map<String, String> nodeAddresses = new HashMap<>();
    nodeAddresses.put("node-with-special-chars", "10.0.0.1");
    nodeAddresses.put("node2", "10.0.0.2");

    FileCollection fc =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            nodeTarPaths,
            nodeAddresses);

    // Retrieve and verify serialization/deserialization works
    FileCollection retrieved = FileCollection.getOrBadRequest(fc.getCollectionUuid());
    assertEquals(nodeTarPaths, retrieved.getNodeTarPaths());
    assertEquals(nodeAddresses, retrieved.getNodeAddresses());
  }

  @Test
  public void testEmptyNodePaths() {
    Map<String, String> emptyPaths = new HashMap<>();
    Map<String, String> emptyAddresses = new HashMap<>();

    FileCollection fc =
        FileCollection.create(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            emptyPaths,
            emptyAddresses);

    assertNotNull(fc);
    assertTrue(fc.getNodeTarPaths().isEmpty());
    assertTrue(fc.getNodeAddresses().isEmpty());
  }
}
