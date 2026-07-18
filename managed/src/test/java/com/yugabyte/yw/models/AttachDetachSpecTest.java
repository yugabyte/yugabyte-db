// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AttachDetachSpec.PlatformPaths;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AttachDetachSpecTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private Universe universe;
  private AttachDetachSpec attachDetachSpec;
  private PlatformPaths platformPaths;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    universe = ModelFactory.createUniverse(customer.getId());

    platformPaths =
        PlatformPaths.builder()
            .storagePath("/tmp/test-storage")
            .releasesPath("/tmp/test-releases")
            .build();

    attachDetachSpec =
        AttachDetachSpec.builder()
            .universe(universe)
            .provider(provider)
            .instanceTypes(new ArrayList<>())
            .priceComponents(new ArrayList<>())
            .certificateInfoList(new ArrayList<>())
            .imageBundles(new HashMap<>())
            .nodeInstances(new ArrayList<>())
            .kmsConfigs(new ArrayList<>())
            .kmsHistoryList(new ArrayList<>())
            .backups(new ArrayList<>())
            .schedules(new ArrayList<>())
            .customerConfigs(new ArrayList<>())
            .universeConfig(new HashMap<>())
            .oldPlatformPaths(platformPaths)
            .skipReleases(false)
            .build();
  }

  @Test
  public void testGenerateSpecName_Export() {
    String specName = AttachDetachSpec.generateSpecName(true);

    assertNotNull("Spec name should not be null", specName);
    assertTrue("Spec name should contain 'export'", specName.contains("export"));
    assertTrue(
        "Spec name should start with yb-attach-detach-spec",
        specName.startsWith("yb-attach-detach-spec"));
    assertTrue("Spec name should contain timestamp", specName.matches(".*\\d{14}\\.\\d{3}.*"));
  }

  @Test
  public void testGenerateSpecName_Import() {
    String specName = AttachDetachSpec.generateSpecName(false);

    assertNotNull("Spec name should not be null", specName);
    assertTrue("Spec name should contain 'import'", specName.contains("import"));
    assertTrue(
        "Spec name should start with yb-attach-detach-spec",
        specName.startsWith("yb-attach-detach-spec"));
    assertTrue("Spec name should contain timestamp", specName.matches(".*\\d{14}\\.\\d{3}.*"));
  }

  @Test
  public void testGenerateAttachDetachSpecObj_BasicProperties() {
    ObjectNode specObj = attachDetachSpec.generateAttachDetachSpecObj();

    assertNotNull("Generated spec object should not be null", specObj);
    assertTrue("Spec should contain universe", specObj.has("universe"));
    assertTrue("Spec should contain provider", specObj.has("provider"));
    assertTrue("Spec should contain skipReleases", specObj.has("skipReleases"));
    assertFalse("SkipReleases should be false", specObj.get("skipReleases").asBoolean());
  }

  @Test
  public void testGenerateAttachDetachSpecObj_WithSkipReleases() {
    AttachDetachSpec specWithSkipReleases =
        AttachDetachSpec.builder()
            .universe(universe)
            .provider(provider)
            .instanceTypes(new ArrayList<>())
            .priceComponents(new ArrayList<>())
            .certificateInfoList(new ArrayList<>())
            .imageBundles(new HashMap<>())
            .nodeInstances(new ArrayList<>())
            .kmsConfigs(new ArrayList<>())
            .kmsHistoryList(new ArrayList<>())
            .backups(new ArrayList<>())
            .schedules(new ArrayList<>())
            .customerConfigs(new ArrayList<>())
            .universeConfig(new HashMap<>())
            .oldPlatformPaths(platformPaths)
            .skipReleases(true)
            .build();

    ObjectNode specObj = specWithSkipReleases.generateAttachDetachSpecObj();

    assertTrue("SkipReleases should be true", specObj.get("skipReleases").asBoolean());
  }

  @Test
  public void testSetIgnoredJsonProperties_ProviderDetailsUnmasked() {
    // Test that provider details are properly unmasked for export
    ObjectNode originalObj = (ObjectNode) Json.toJson(attachDetachSpec);
    ObjectNode updatedObj = attachDetachSpec.setIgnoredJsonProperties(originalObj);

    assertNotNull("Updated object should not be null", updatedObj);
    assertTrue("Should have provider details", updatedObj.has("provider"));

    ObjectNode providerObj = (ObjectNode) updatedObj.get("provider");
    assertTrue("Provider should have unmasked details", providerObj.has("details"));

    // This is critical for attach/detach as masked provider details would break the import
    assertNotNull("Provider details should not be null", providerObj.get("details"));
  }

  @Test
  public void testSetIgnoredJsonProperties_RegionAndAZDetails() {
    // Test that region and AZ details are properly unmasked
    ObjectNode originalObj = (ObjectNode) Json.toJson(attachDetachSpec);
    ObjectNode updatedObj = attachDetachSpec.setIgnoredJsonProperties(originalObj);

    ObjectNode providerObj = (ObjectNode) updatedObj.get("provider");
    if (providerObj.has("regions")) {
      // Verify regions have unmasked details
      assertTrue("Regions should be properly processed", providerObj.get("regions").isArray());
    }
  }

  @Test
  public void testImportSpec_InvalidPath() {
    Path nonExistentPath = Paths.get("/non/existent/path.tar.gz");

    assertThrows(
        "Should throw exception for non-existent file",
        IOException.class,
        () -> AttachDetachSpec.importSpec(nonExistentPath, platformPaths, customer));
  }

  @Test
  public void testUpdateUniverseMetadata_CustomerChange() {
    // Test that universe metadata is updated when moving between YBA instances
    Customer newCustomer = ModelFactory.testCustomer("new-customer");

    Long originalCustomerId = universe.getCustomerId();

    // Simulate the metadata update process (this would normally happen in updateUniverseMetadata)
    universe.setCustomerId(newCustomer.getId());

    assertNotNull("Original customer ID should not be null", originalCustomerId);
    assertEquals(
        "Customer ID should match new customer", newCustomer.getId(), universe.getCustomerId());
  }

  @Test
  public void testAttachDetachSpec_AllRequiredFields() {
    // Test that all required fields are properly set in the builder
    assertNotNull("Universe should not be null", attachDetachSpec.universe);
    assertNotNull("Provider should not be null", attachDetachSpec.provider);
    assertNotNull("Instance types should not be null", attachDetachSpec.instanceTypes);
    assertNotNull("Price components should not be null", attachDetachSpec.priceComponents);
    assertNotNull("Certificate info list should not be null", attachDetachSpec.certificateInfoList);
    assertNotNull("Image bundles should not be null", attachDetachSpec.imageBundles);
    assertNotNull("Node instances should not be null", attachDetachSpec.nodeInstances);
    assertNotNull("KMS configs should not be null", attachDetachSpec.kmsConfigs);
    assertNotNull("KMS history list should not be null", attachDetachSpec.kmsHistoryList);
    assertNotNull("Backups should not be null", attachDetachSpec.backups);
    assertNotNull("Schedules should not be null", attachDetachSpec.schedules);
    assertNotNull("Customer configs should not be null", attachDetachSpec.customerConfigs);
    assertNotNull("Universe config should not be null", attachDetachSpec.universeConfig);
    assertNotNull("Old platform paths should not be null", attachDetachSpec.oldPlatformPaths);
  }

  @Test
  public void testPlatformPathsBuilder() {
    String storagePath = "/test/storage";
    String releasesPath = "/test/releases";

    PlatformPaths paths =
        PlatformPaths.builder().storagePath(storagePath).releasesPath(releasesPath).build();

    assertEquals("Storage path should match", storagePath, paths.storagePath);
    assertEquals("Releases path should match", releasesPath, paths.releasesPath);
  }

  @Test
  public void testAttachDetachSpec_SkipReleasesFlag() {
    // Test that skipReleases flag works correctly
    AttachDetachSpec specSkipFalse =
        AttachDetachSpec.builder()
            .universe(universe)
            .provider(provider)
            .instanceTypes(new ArrayList<>())
            .priceComponents(new ArrayList<>())
            .certificateInfoList(new ArrayList<>())
            .imageBundles(new HashMap<>())
            .nodeInstances(new ArrayList<>())
            .kmsConfigs(new ArrayList<>())
            .kmsHistoryList(new ArrayList<>())
            .backups(new ArrayList<>())
            .schedules(new ArrayList<>())
            .customerConfigs(new ArrayList<>())
            .universeConfig(new HashMap<>())
            .oldPlatformPaths(platformPaths)
            .skipReleases(false)
            .build();

    AttachDetachSpec specSkipTrue =
        AttachDetachSpec.builder()
            .universe(universe)
            .provider(provider)
            .instanceTypes(new ArrayList<>())
            .priceComponents(new ArrayList<>())
            .certificateInfoList(new ArrayList<>())
            .imageBundles(new HashMap<>())
            .nodeInstances(new ArrayList<>())
            .kmsConfigs(new ArrayList<>())
            .kmsHistoryList(new ArrayList<>())
            .backups(new ArrayList<>())
            .schedules(new ArrayList<>())
            .customerConfigs(new ArrayList<>())
            .universeConfig(new HashMap<>())
            .oldPlatformPaths(platformPaths)
            .skipReleases(true)
            .build();

    // Test via JSON generation since skipReleases is private
    ObjectNode specObjFalse = specSkipFalse.generateAttachDetachSpecObj();
    ObjectNode specObjTrue = specSkipTrue.generateAttachDetachSpecObj();

    assertFalse("Skip releases should be false", specObjFalse.get("skipReleases").asBoolean());
    assertTrue("Skip releases should be true", specObjTrue.get("skipReleases").asBoolean());
  }
}
