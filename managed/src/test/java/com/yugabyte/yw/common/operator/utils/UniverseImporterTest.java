// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseImporterTest extends FakeDBApplication {

  @Mock private YbcManager mockYbcManager;

  private UniverseImporter universeImporter;
  private Customer testCustomer;
  private Universe testUniverse;
  private YBUniverseSpec testSpec;

  @Before
  public void setUp() {
    universeImporter = new UniverseImporter(mockYbcManager);
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("test-universe", testCustomer.getId());
    testSpec = new YBUniverseSpec();
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_NullOverrides() {
    // Setup: Set universeOverrides to null
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = null;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute
    universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse);

    // Verify: No KubernetesOverrides should be set
    assertNull(testSpec.getKubernetesOverrides());
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_EmptyOverrides() {
    // Setup: Set universeOverrides to empty string
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = "";
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute
    universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse);

    // Verify: No KubernetesOverrides should be set
    assertNull(testSpec.getKubernetesOverrides());
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_ValidYaml() {
    // Setup: Set valid YAML overrides
    String validYaml =
        "nodeSelector:\n"
            + "  kubernetes.io/os: linux\n"
            + "resource:\n"
            + "  master:\n"
            + "    limits:\n"
            + "      cpu: '2'\n"
            + "      memory: 4Gi\n"
            + "  tserver:\n"
            + "    limits:\n"
            + "      cpu: '4'\n"
            + "      memory: 8Gi\n";

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = validYaml;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute
    universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse);

    // Verify: KubernetesOverrides should be set correctly
    KubernetesOverrides overrides = testSpec.getKubernetesOverrides();
    assertNotNull(overrides);
    assertNotNull(overrides.getNodeSelector());
    assertEquals("linux", overrides.getNodeSelector().get("kubernetes.io/os"));
    assertNotNull(overrides.getResource());
    assertNotNull(overrides.getResource().getMaster());
    assertNotNull(overrides.getResource().getMaster().getLimits());
    assertEquals("2", overrides.getResource().getMaster().getLimits().getCpu().getStrVal());
    assertEquals("4Gi", overrides.getResource().getMaster().getLimits().getMemory().getStrVal());
    assertNotNull(overrides.getResource().getTserver());
    assertNotNull(overrides.getResource().getTserver().getLimits());
    assertEquals("4", overrides.getResource().getTserver().getLimits().getCpu().getStrVal());
    assertEquals("8Gi", overrides.getResource().getTserver().getLimits().getMemory().getStrVal());
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_ValidYamlWithAdditionalProperties() {
    // Setup: Set valid YAML overrides with additional properties
    String validYaml =
        "nodeSelector:\n"
            + "  kubernetes.io/os: linux\n"
            + "customProperty:\n"
            + "  key: value\n"
            + "anotherProperty: test\n";

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = validYaml;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute
    universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse);

    // Verify: KubernetesOverrides should be set correctly with additional properties
    KubernetesOverrides overrides = testSpec.getKubernetesOverrides();
    assertNotNull(overrides);
    assertNotNull(overrides.getNodeSelector());
    assertEquals("linux", overrides.getNodeSelector().get("kubernetes.io/os"));
    assertNotNull(overrides.getAdditionalProperties());
    assertTrue(overrides.getAdditionalProperties().containsKey("customProperty"));
    assertTrue(overrides.getAdditionalProperties().containsKey("anotherProperty"));
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_InvalidYaml() {
    // Setup: Set invalid YAML overrides
    String invalidYaml = "invalid: yaml: content: [";

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = invalidYaml;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute and Verify: Should throw RuntimeException
    RuntimeException exception =
        assertThrows(
            RuntimeException.class,
            () -> universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse));

    assertEquals("Error in setting KubernetesOverridesSpecFromUniverse", exception.getMessage());
    assertNotNull(exception.getCause());
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_JsonProcessingException() {
    // Setup: Use malformed YAML that will cause parsing issues                     bad indent here.
    String malformedYaml =
        "nodeSelector:\n"
            + "  kubernetes.io/os: linux\n"
            + "service:\n"
            + "  type: ClusterIP\n"
            + "resource:\n"
            + "  master:\n"
            + "  limits:\n"
            + "    cpu: 'invalid_cpu_value'\n"
            + "    memory: 'invalid_memory_value'\n";

    Universe.UniverseUpdater malformedUpdater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = malformedYaml;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), malformedUpdater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute and Verify: Should throw RuntimeException
    RuntimeException exception =
        assertThrows(
            RuntimeException.class,
            () -> universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse));

    assertEquals("Error in setting KubernetesOverridesSpecFromUniverse", exception.getMessage());
    assertNotNull(exception.getCause());
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_ComplexYaml() {
    // Setup: Set complex YAML overrides with multiple sections
    String complexYaml =
        "nodeSelector:\n"
            + "  kubernetes.io/os: linux\n"
            + "  node-role.kubernetes.io/worker: \"\"\n"
            + "resource:\n"
            + "  master:\n"
            + "    limits:\n"
            + "      cpu: '2'\n"
            + "      memory: 4Gi\n"
            + "    requests:\n"
            + "      cpu: '1'\n"
            + "      memory: 2Gi\n"
            + "  tserver:\n"
            + "    limits:\n"
            + "      cpu: '4'\n"
            + "      memory: 8Gi\n"
            + "    requests:\n"
            + "      cpu: '2'\n"
            + "      memory: 4Gi\n"
            + "master:\n"
            + "  extraEnv:\n"
            + "    - name: TEST_ENV\n"
            + "      value: test_value\n"
            + "tserver:\n"
            + "  extraEnv:\n"
            + "    - name: TEST_ENV\n"
            + "      value: test_value\n";

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = complexYaml;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute
    universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse);

    // Verify: KubernetesOverrides should be set correctly with all sections
    KubernetesOverrides overrides = testSpec.getKubernetesOverrides();
    assertNotNull(overrides);

    // Verify nodeSelector
    assertNotNull(overrides.getNodeSelector());
    assertEquals("linux", overrides.getNodeSelector().get("kubernetes.io/os"));
    assertEquals("", overrides.getNodeSelector().get("node-role.kubernetes.io/worker"));

    // Verify resource limits and requests
    assertNotNull(overrides.getResource());
    assertNotNull(overrides.getResource().getMaster());
    assertNotNull(overrides.getResource().getMaster().getLimits());
    assertEquals("2", overrides.getResource().getMaster().getLimits().getCpu().getStrVal());
    assertEquals("4Gi", overrides.getResource().getMaster().getLimits().getMemory().getStrVal());
    assertNotNull(overrides.getResource().getMaster().getRequests());
    assertEquals("1", overrides.getResource().getMaster().getRequests().getCpu().getStrVal());
    assertEquals("2Gi", overrides.getResource().getMaster().getRequests().getMemory().getStrVal());

    assertNotNull(overrides.getResource().getTserver());
    assertNotNull(overrides.getResource().getTserver().getLimits());
    assertEquals("4", overrides.getResource().getTserver().getLimits().getCpu().getStrVal());
    assertEquals("8Gi", overrides.getResource().getTserver().getLimits().getMemory().getStrVal());
    assertNotNull(overrides.getResource().getTserver().getRequests());
    assertEquals("2", overrides.getResource().getTserver().getRequests().getCpu().getStrVal());
    assertEquals("4Gi", overrides.getResource().getTserver().getRequests().getMemory().getStrVal());

    // Verify master and tserver sections
    assertNotNull(overrides.getMaster());
    assertNotNull(overrides.getTserver());
  }

  @Test
  public void testSetKubernetesOverridesSpecFromUniverse_MinimalYaml() {
    // Setup: Set minimal YAML overrides
    String minimalYaml = "nodeSelector:\n  kubernetes.io/os: linux\n";

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.universeOverrides = minimalYaml;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Execute
    universeImporter.setKubernetesOverridesSpecFromUniverse(testSpec, testUniverse);

    // Verify: KubernetesOverrides should be set correctly with minimal data
    KubernetesOverrides overrides = testSpec.getKubernetesOverrides();
    assertNotNull(overrides);
    assertNotNull(overrides.getNodeSelector());
    assertEquals("linux", overrides.getNodeSelector().get("kubernetes.io/os"));
    // Other fields should be null for minimal YAML
    assertNull(overrides.getResource());
    assertNull(overrides.getMaster());
    assertNull(overrides.getTserver());
  }
}
