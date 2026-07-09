// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class UniverseCRUDHandlerTest extends FakeDBApplication {

  private static final String MASTER_FIELDS_ERROR =
      "masterDeviceInfo and masterInstanceType can only be set when dedicated nodes for "
          + "master and tserver are selected.";

  private Customer customer;
  private UniverseCRUDHandler universeCRUDHandler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    ModelFactory.awsProvider(customer);
    universeCRUDHandler = app.injector().instanceOf(UniverseCRUDHandler.class);
  }

  @Parameters({
    "enableYSQL",
    "enableYSQLAuth",
    "enableYCQL",
    "enableYCQLAuth",
    "enableYEDIS",
    "enableClientToNodeEncrypt",
    "enableNodeToNodeEncrypt",
    "assignPublicIP"
  })
  @Test
  public void testReadonlyClusterConsistency(String paramName) throws NoSuchFieldException {
    Field field = UniverseDefinitionTaskParams.UserIntent.class.getDeclaredField(paramName);
    if (field.getType().equals(boolean.class) || field.getType().equals(Boolean.class)) {
      for (boolean primaryVal : Arrays.asList(true, false)) {
        for (boolean readonlyVal : Arrays.asList(true, false)) {
          try {
            UniverseDefinitionTaskParams.UserIntent primary = testIntent();
            field.set(primary, primaryVal);
            UniverseDefinitionTaskParams.Cluster primaryCluster =
                new UniverseDefinitionTaskParams.Cluster(
                    UniverseDefinitionTaskParams.ClusterType.PRIMARY, primary);
            UniverseDefinitionTaskParams.UserIntent readonly = testIntent();
            field.set(readonly, readonlyVal);
            UniverseDefinitionTaskParams.Cluster readonlyCluster =
                new UniverseDefinitionTaskParams.Cluster(
                    UniverseDefinitionTaskParams.ClusterType.ASYNC, readonly);
            if (primaryVal == readonlyVal) {
              UniverseCRUDHandler.validateConsistency(primaryCluster, readonlyCluster);
            } else {
              assertThrows(
                  PlatformServiceException.class,
                  () -> UniverseCRUDHandler.validateConsistency(primaryCluster, readonlyCluster));
            }
          } catch (IllegalAccessException e) {
            throw new RuntimeException(e);
          }
        }
      }
    } else {
      throw new IllegalArgumentException("Unsupported type " + field.getType());
    }
  }

  private UniverseDefinitionTaskParams.UserIntent testIntent() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    return userIntent;
  }

  @Test
  public void checkInstanceTypeConsistency_passWhenNodesMatchIntent() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 3);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = "c5.4xlarge";
          for (NodeDetails n : univ.getUniverseDetails().getNodesInCluster(primaryUuid)) {
            n.cloudInfo.instance_type = "c5.4xlarge";
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Test
  public void checkInstanceTypeConsistency_failsOnDrift() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 3);
    final UUID universeUuid = u.getUniverseUUID();
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        universeUuid,
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = "c5.4xlarge";
          List<NodeDetails> nodes =
              univ.getUniverseDetails().getNodesInCluster(primaryUuid).stream()
                  .collect(Collectors.toList());
          nodes.get(0).cloudInfo.instance_type = "c5.4xlarge";
          nodes.get(1).cloudInfo.instance_type = "c5.9xlarge";
          nodes.get(2).cloudInfo.instance_type = "c5.9xlarge";
          nodes.get(0).nodeName = "host-n0";
          nodes.get(1).nodeName = "host-n1";
          nodes.get(2).nodeName = "host-n2";
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    final Universe universeAfterSave = Universe.getOrBadRequest(universeUuid);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> UniverseCRUDHandler.checkInstanceTypeConsistency(universeAfterSave));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains("host-n1"));
    assertTrue(ex.getUserVisibleMessage().contains("c5.9xlarge"));
    assertTrue(ex.getUserVisibleMessage().contains("c5.4xlarge"));
  }

  @Test
  public void checkInstanceTypeConsistency_passWithAzOverride() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 2);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    UUID azUuid = UUID.randomUUID();
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          Cluster primary = univ.getUniverseDetails().getPrimaryCluster();
          primary.userIntent.instanceType = "c5.4xlarge";
          UserIntentOverrides overrides = new UserIntentOverrides();
          AZOverrides azOverrides = new AZOverrides();
          azOverrides.setInstanceType("c5.9xlarge");
          overrides.setAzOverrides(new HashMap<>(Map.of(azUuid, azOverrides)));
          primary.userIntent.setUserIntentOverrides(overrides);
          for (NodeDetails n : univ.getUniverseDetails().getNodesInCluster(primaryUuid)) {
            n.azUuid = azUuid;
            n.cloudInfo.instance_type = "c5.9xlarge";
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Test
  public void checkInstanceTypeConsistency_failsWhenNodeDoesNotMatchAzOverride() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 1);
    final UUID universeUuid = u.getUniverseUUID();
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    UUID azUuid = UUID.randomUUID();
    Universe.saveDetails(
        universeUuid,
        univ -> {
          Cluster primary = univ.getUniverseDetails().getPrimaryCluster();
          primary.userIntent.instanceType = "c5.4xlarge";
          UserIntentOverrides overrides = new UserIntentOverrides();
          AZOverrides azOverrides = new AZOverrides();
          azOverrides.setInstanceType("c5.9xlarge");
          overrides.setAzOverrides(new HashMap<>(Map.of(azUuid, azOverrides)));
          primary.userIntent.setUserIntentOverrides(overrides);
          NodeDetails n =
              univ.getUniverseDetails().getNodesInCluster(primaryUuid).iterator().next();
          n.azUuid = azUuid;
          n.nodeName = "host-n0";
          n.cloudInfo.instance_type = "m5.large";
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    final Universe universeAfterSave = Universe.getOrBadRequest(universeUuid);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> UniverseCRUDHandler.checkInstanceTypeConsistency(universeAfterSave));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains("m5.large"));
    assertTrue(ex.getUserVisibleMessage().contains("c5.9xlarge"));
  }

  @Test
  public void checkInstanceTypeConsistency_passDedicatedMaster() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 2);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    List<NodeDetails> nodes =
        u.getUniverseDetails().getNodesInCluster(primaryUuid).stream().collect(Collectors.toList());
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = "c5.4xlarge";
          intent.masterInstanceType = "m5.2xlarge";
          intent.dedicatedNodes = true;
          NodeDetails master = nodes.get(0);
          master.nodeName = "host-master";
          master.isMaster = true;
          master.dedicatedTo = ServerType.MASTER;
          master.cloudInfo.instance_type = "m5.2xlarge";
          NodeDetails ts = nodes.get(1);
          ts.nodeName = "host-ts";
          ts.isTserver = true;
          ts.cloudInfo.instance_type = "c5.4xlarge";
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Test
  public void checkInstanceTypeConsistency_skipsKubernetes() {
    ModelFactory.newProvider(customer, Common.CloudType.kubernetes);
    Universe u =
        ModelFactory.createUniverse(
            "k8s-u", UUID.randomUUID(), customer.getId(), Common.CloudType.kubernetes);
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 2);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = "small";
          for (NodeDetails n : univ.getUniverseDetails().getNodesInCluster(primaryUuid)) {
            n.cloudInfo.instance_type = "huge";
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Parameters({
    // enableYSQL, enableYCQL, skipYcqlPrecheck, expectError, expectedFragment
    "true,  false, false, false, ",
    "true,  false, true,  false, ",
    "true,  true,  false, true,  YCQL API should be disabled",
    "true,  true,  true,  false, ",
    "false, false, false, true,  YSQL API should be enabled",
    "false, false, true,  true,  YSQL API should be enabled",
    "false, true,  false, true,  YSQL API should be enabled",
    "false, true,  true,  true,  YSQL API should be enabled",
  })
  @Test
  public void validateMultiTenancyApiConfig_truthTable(
      boolean enableYSQL,
      boolean enableYCQL,
      boolean skipYcqlPrecheck,
      boolean expectError,
      String expectedFragment) {
    if (!expectError) {
      UniverseCRUDHandler.validateMultiTenancyApiConfig(enableYSQL, enableYCQL, skipYcqlPrecheck);
      return;
    }
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                UniverseCRUDHandler.validateMultiTenancyApiConfig(
                    enableYSQL, enableYCQL, skipYcqlPrecheck));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(
        "Expected message to contain '"
            + expectedFragment
            + "', got: "
            + ex.getUserVisibleMessage(),
        ex.getUserVisibleMessage().contains(expectedFragment));
  }

  @Test
  public void validateMultiTenancyApiConfig_skipYcqlPrecheckBypassesYcqlOn() {
    // The regression case: skipYcqlPrecheck=true with YSQL on and YCQL on must pass.
    UniverseCRUDHandler.validateMultiTenancyApiConfig(
        true /* enableYSQL */, true /* enableYCQL */, true /* skipYcqlPrecheck */);
  }

  @Test
  public void validateMultiTenancyApiConfig_ysqlOffReportsYsqlError() {
    // Even when skipYcqlPrecheck would bypass the YCQL check, YSQL-off must still fail,
    // and the error message must point at YSQL (not the legacy combined string).
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                UniverseCRUDHandler.validateMultiTenancyApiConfig(
                    false /* enableYSQL */, false /* enableYCQL */, true /* skipYcqlPrecheck */));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains("YSQL API should be enabled"));
  }

  @Test
  public void configureRejectsMasterDeviceInfoWhenDedicatedNodesDisabled() {
    UniverseConfigureTaskParams taskParams = buildConfigureTaskParams(false, true, false);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> universeCRUDHandler.configure(customer, taskParams));

    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains(MASTER_FIELDS_ERROR));
  }

  @Test
  public void configureRejectsMasterInstanceTypeWhenDedicatedNodesDisabled() {
    UniverseConfigureTaskParams taskParams = buildConfigureTaskParams(false, false, true);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> universeCRUDHandler.configure(customer, taskParams));

    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains(MASTER_FIELDS_ERROR));
  }

  @Test
  public void checkInstanceTypeConsistency_skipsToBeAddedAndToBeRemoved() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 1);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = "c5.4xlarge";
          NodeDetails live =
              univ.getUniverseDetails().getNodesInCluster(primaryUuid).iterator().next();
          live.nodeName = "host-live";
          live.state = NodeState.Live;
          live.cloudInfo.instance_type = "c5.4xlarge";
          NodeDetails toAdd = new NodeDetails();
          toAdd.nodeName = "host-add";
          toAdd.state = NodeState.ToBeAdded;
          toAdd.placementUuid = primaryUuid;
          toAdd.cloudInfo = new CloudSpecificInfo();
          toAdd.cloudInfo.instance_type = "wrong";
          toAdd.azUuid = live.azUuid;
          univ.getUniverseDetails().nodeDetailsSet.add(toAdd);
          NodeDetails toRemove = new NodeDetails();
          toRemove.nodeName = "host-rem";
          toRemove.state = NodeState.ToBeRemoved;
          toRemove.placementUuid = primaryUuid;
          toRemove.cloudInfo = new CloudSpecificInfo();
          toRemove.cloudInfo.instance_type = "wrong";
          toRemove.azUuid = live.azUuid;
          univ.getUniverseDetails().nodeDetailsSet.add(toRemove);
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  private UniverseConfigureTaskParams buildConfigureTaskParams(
      boolean dedicatedNodes, boolean setMasterDeviceInfo, boolean setMasterInstanceType) {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = dedicatedNodes;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    if (setMasterDeviceInfo) {
      userIntent.masterDeviceInfo = ApiUtils.getDummyDeviceInfo(1, 50);
    }
    if (setMasterInstanceType) {
      userIntent.masterInstanceType = "m5.large";
    }

    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.currentClusterType = ClusterType.PRIMARY;
    taskParams.clusterOperation = ClusterOperationType.EDIT;
    taskParams.clusters.add(cluster);
    return taskParams;
  }
}
