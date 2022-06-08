// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import java.lang.reflect.Field;
import java.util.Arrays;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class UniverseCRUDHandlerTest {

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
}
