// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import java.lang.reflect.Field;
import java.util.Arrays;
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

  private UniverseDefinitionTaskParams.UserIntent testIntent() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    return userIntent;
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
