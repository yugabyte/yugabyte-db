// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.GFlagsValidation.AutoFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagsValidation.AutoFlagsPerServer;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.util.Arrays;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CheckUpgradeTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    TestHelper.updateUniverseVersion(defaultUniverse, "new-version");
  }

  @Test
  public void testAutoFlagCheckForUpgradeAmongNonCompatibleVersion() {
    TestHelper.updateUniverseVersion(defaultUniverse, "2.14.0.0");
    CheckUpgrade.Params params = new CheckUpgrade.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "2.16.0.0";
    CheckUpgrade task = AbstractTaskBase.createTask(CheckUpgrade.class);
    task.initialize(params);
    task.run();
  }

  @Test
  public void testAutoFlagCheckForUpgradeToNonCompatibleVersion() {
    CheckUpgrade.Params params = new CheckUpgrade.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "2.14.0.0";
    CheckUpgrade task = AbstractTaskBase.createTask(CheckUpgrade.class);
    task.initialize(params);
    PlatformServiceException pe = assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(BAD_REQUEST, pe.getHttpStatus());
    assertEquals(
        "Cannot upgrade DB to version which does not contains auto flags", pe.getMessage());
  }

  @Test
  public void testFailedAutoFlagFileExtraction() throws Exception {
    CheckUpgrade.Params params = new CheckUpgrade.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "new-version";
    doThrow(new IOException("Error occurred while extracting auto_flag.json file"))
        .when(mockGFlagsValidation)
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());
    CheckUpgrade task = AbstractTaskBase.createTask(CheckUpgrade.class);
    task.initialize(params);
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(INTERNAL_SERVER_ERROR, exception.getHttpStatus());
    assertEquals("Error occurred while extracting auto_flag.json file", exception.getMessage());
  }

  @Test
  public void testGetAutoFlagConfigError() throws Exception {
    CheckUpgrade.Params params = new CheckUpgrade.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "new-version";
    when(mockAutoFlagUtil.getPromotedAutoFlags(any(), any(), anyInt()))
        .thenThrow(
            new PlatformServiceException(INTERNAL_SERVER_ERROR, "Unable to get auto flags config"));
    CheckUpgrade task = AbstractTaskBase.createTask(CheckUpgrade.class);
    task.initialize(params);
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(INTERNAL_SERVER_ERROR, exception.getHttpStatus());
    assertEquals("Unable to get auto flags config", exception.getMessage());
  }

  @Test
  public void testMissingAutoFlag() throws Exception {
    CheckUpgrade.Params params = new CheckUpgrade.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "new-version";
    when(mockAutoFlagUtil.getPromotedAutoFlags(any(), any(), anyInt()))
        .thenReturn(ImmutableSet.of("FLAG_1", "FLAG_2"));
    AutoFlagDetails flag = new AutoFlagDetails();
    flag.name = "FLAG_2";
    AutoFlagsPerServer flagsPerServer = new AutoFlagsPerServer();
    flagsPerServer.autoFlagDetails = Arrays.asList(flag);
    when(mockGFlagsValidation.extractAutoFlags(any(), anyString())).thenReturn(flagsPerServer);
    CheckUpgrade task = AbstractTaskBase.createTask(CheckUpgrade.class);
    task.initialize(params);
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
    assertEquals(
        "FLAG_1 is not present in the requested db version new-version", exception.getMessage());
  }

  @Test
  public void testCheckSuccess() throws Exception {
    CheckUpgrade.Params params = new CheckUpgrade.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "new-version";
    when(mockAutoFlagUtil.getPromotedAutoFlags(any(), any(), anyInt()))
        .thenReturn(ImmutableSet.of("FLAG_1"));
    AutoFlagDetails flag = new AutoFlagDetails();
    flag.name = "FLAG_1";
    AutoFlagsPerServer flagsPerServer = new AutoFlagsPerServer();
    flagsPerServer.autoFlagDetails = Arrays.asList(flag);
    when(mockGFlagsValidation.extractAutoFlags(any(), anyString())).thenReturn(flagsPerServer);
    CheckUpgrade task = AbstractTaskBase.createTask(CheckUpgrade.class);
    task.initialize(params);
    task.run();
  }
}
