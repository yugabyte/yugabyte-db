package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class CheckSoftwareVersionTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private NodeDetails node;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    node = new NodeDetails();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "1.2.3.4";
    node.nodeName = "node-1";
    details.nodeDetailsSet.add(node);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
  }

  @Test
  @Parameters({
    "2.17.0.1-b1, 2.17.0.1, 9",
    "2.17.0.0-b1, 2.17.0.2, 1",
    "2.18.0.0-b1, 2.17.0.0, 1",
    "2.18.0.0-b1, 2.17.0.0, 1"
  })
  public void testDifferentVersionFail(String version1, String nodeVersion, String nodeBuild) {
    updateUniverseVersion(defaultUniverse, version1);
    try {
      ObjectNode objectNode = Json.newObject();
      objectNode.put("version_number", nodeVersion);
      objectNode.put("build_number", nodeBuild);
      when(mockApiHelper.getRequest(anyString())).thenReturn(objectNode);
    } catch (Exception ignored) {
      fail();
    }
    CheckSoftwareVersion task = AbstractTaskBase.createTask(CheckSoftwareVersion.class);
    CheckSoftwareVersion.Params params = new CheckSoftwareVersion.Params();
    params.universeUUID = defaultUniverse.universeUUID;
    params.nodeName = node.nodeName;
    params.requiredVersion = version1;
    task.initialize(params);
    PlatformServiceException pe = assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(BAD_REQUEST, pe.getHttpStatus());
    String newYbSoftwareVersion = nodeVersion + "-b" + nodeBuild;
    assertEquals(
        "Expected version: "
            + version1
            + " but found: "
            + newYbSoftwareVersion
            + " on node: "
            + node.nodeName,
        pe.getMessage());
  }

  @Test
  @Parameters({
    "2.17.0.1-b1, 2.17.0.1, 1",
    "2.17.0.0-b1, 2.17.0.0, PRE_RELEASE",
    "2.17.0.0-PRE_RELEASE, 2.17.0.0, b1"
  })
  public void testCheckSoftwareVersionSuccess(
      String version, String nodeVersion, String nodeBuild) {
    updateUniverseVersion(defaultUniverse, version);
    try {
      ObjectNode objectNode = Json.newObject();
      objectNode.put("version_number", nodeVersion);
      objectNode.put("build_number", nodeBuild);
      when(mockApiHelper.getRequest(anyString())).thenReturn(objectNode);
    } catch (Exception ignored) {
      fail();
    }
    CheckSoftwareVersion task = AbstractTaskBase.createTask(CheckSoftwareVersion.class);
    CheckSoftwareVersion.Params params = new CheckSoftwareVersion.Params();
    params.universeUUID = defaultUniverse.universeUUID;
    params.nodeName = node.nodeName;
    params.requiredVersion = version;
    task.initialize(params);
    task.run();
  }

  @Test
  public void testMissingVersionDetails() {
    try {
      ObjectNode objectNode = Json.newObject();
      when(mockApiHelper.getRequest(anyString())).thenReturn(objectNode);
      CheckSoftwareVersion task = AbstractTaskBase.createTask(CheckSoftwareVersion.class);
      CheckSoftwareVersion.Params params = new CheckSoftwareVersion.Params();
      params.universeUUID = defaultUniverse.universeUUID;
      params.nodeName = node.nodeName;
      params.requiredVersion = "2.17.0.0-b1";
      task.initialize(params);
      PlatformServiceException pe = assertThrows(PlatformServiceException.class, () -> task.run());
      objectNode.put("version_number", "2.17.0.0");
      when(mockApiHelper.getRequest(anyString())).thenReturn(objectNode);
      assertEquals(INTERNAL_SERVER_ERROR, pe.getHttpStatus());
      assertEquals(
          "Could not find version number on address " + node.cloudInfo.private_ip, pe.getMessage());
      pe = assertThrows(PlatformServiceException.class, () -> task.run());
      assertEquals(INTERNAL_SERVER_ERROR, pe.getHttpStatus());
      assertEquals(
          "Could not find build number on address " + node.cloudInfo.private_ip, pe.getMessage());
      objectNode.put("build_number", "1");
      when(mockApiHelper.getRequest(anyString())).thenReturn(objectNode);
      task.run();
    } catch (Exception ignored) {
      fail();
    }
  }

  private void updateUniverseVersion(Universe universe, String version) {
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.ybSoftwareVersion = version;
    details.upsertPrimaryCluster(userIntent, null);
    universe.setUniverseDetails(details);
    universe.save();
  }
}
