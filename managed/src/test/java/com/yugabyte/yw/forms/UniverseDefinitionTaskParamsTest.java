// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.Test;

public class UniverseDefinitionTaskParamsTest {

  @Test
  public void testUpdateAZVolumeOverrides_BasicCase() {
    // Test basic case: updating volume overrides from other UserIntent
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    UUID az2 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);
    azs.add(az2);

    // Setup other UserIntent with device info
    UserIntentOverrides otherOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> otherAzOverrides = new HashMap<>();

    DeviceInfo deviceInfo1 = new DeviceInfo();
    deviceInfo1.volumeSize = 100;
    deviceInfo1.numVolumes = 1;

    DeviceInfo deviceInfo2 = new DeviceInfo();
    deviceInfo2.volumeSize = 200;
    deviceInfo2.numVolumes = 2;

    AZOverrides azOverride1 = new AZOverrides();
    azOverride1.setDeviceInfo(deviceInfo1);

    AZOverrides azOverride2 = new AZOverrides();
    azOverride2.setDeviceInfo(deviceInfo2);

    otherAzOverrides.put(az1, azOverride1);
    otherAzOverrides.put(az2, azOverride2);
    otherOverrides.setAzOverrides(otherAzOverrides);
    otherIntent.setUserIntentOverrides(otherOverrides);

    // Execute
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, null, false);

    // Verify
    UserIntentOverrides currentOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(currentOverrides);
    assertNotNull(currentOverrides.getAzOverrides());
    assertEquals(2, currentOverrides.getAzOverrides().size());

    AZOverrides currentAz1 = currentOverrides.getAzOverrides().get(az1);
    assertNotNull(currentAz1);
    assertNotNull(currentAz1.getDeviceInfo());
    assertEquals(Integer.valueOf(100), currentAz1.getDeviceInfo().volumeSize);
    assertEquals(Integer.valueOf(1), currentAz1.getDeviceInfo().numVolumes);

    AZOverrides currentAz2 = currentOverrides.getAzOverrides().get(az2);
    assertNotNull(currentAz2);
    assertNotNull(currentAz2.getDeviceInfo());
    assertEquals(Integer.valueOf(200), currentAz2.getDeviceInfo().volumeSize);
    assertEquals(Integer.valueOf(2), currentAz2.getDeviceInfo().numVolumes);
  }

  @Test
  public void testUpdateAZVolumeOverrides_WithSkipAZs() {
    // Test that skipAZs are properly excluded
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    UUID az2 = UUID.randomUUID();
    UUID az3 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);
    azs.add(az2);
    azs.add(az3);

    Set<UUID> skipAZs = new HashSet<>();
    skipAZs.add(az2);

    // Setup other UserIntent with device info for all AZs
    UserIntentOverrides otherOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> otherAzOverrides = new HashMap<>();

    DeviceInfo deviceInfo1 = new DeviceInfo();
    deviceInfo1.volumeSize = 100;
    DeviceInfo deviceInfo2 = new DeviceInfo();
    deviceInfo2.volumeSize = 200;
    DeviceInfo deviceInfo3 = new DeviceInfo();
    deviceInfo3.volumeSize = 300;

    AZOverrides azOverride1 = new AZOverrides();
    azOverride1.setDeviceInfo(deviceInfo1);
    AZOverrides azOverride2 = new AZOverrides();
    azOverride2.setDeviceInfo(deviceInfo2);
    AZOverrides azOverride3 = new AZOverrides();
    azOverride3.setDeviceInfo(deviceInfo3);

    otherAzOverrides.put(az1, azOverride1);
    otherAzOverrides.put(az2, azOverride2);
    otherAzOverrides.put(az3, azOverride3);
    otherOverrides.setAzOverrides(otherAzOverrides);
    otherIntent.setUserIntentOverrides(otherOverrides);

    // Execute
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, skipAZs, false);

    // Verify: az1 and az3 should be updated, az2 should be skipped
    UserIntentOverrides currentOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(currentOverrides);
    assertNotNull(currentOverrides.getAzOverrides());
    assertEquals(2, currentOverrides.getAzOverrides().size());

    assertNotNull(currentOverrides.getAzOverrides().get(az1));
    assertNull(currentOverrides.getAzOverrides().get(az2));
    assertNotNull(currentOverrides.getAzOverrides().get(az3));

    assertEquals(
        Integer.valueOf(100),
        currentOverrides.getAzOverrides().get(az1).getDeviceInfo().volumeSize);
    assertEquals(
        Integer.valueOf(300),
        currentOverrides.getAzOverrides().get(az3).getDeviceInfo().volumeSize);
  }

  @Test
  public void testUpdateAZVolumeOverrides_WithDedicatedMaster() {
    // Test that isDedicatedMaster=true uses MASTER server type
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);

    // Setup other UserIntent with per-process device info for both MASTER and TSERVER
    UserIntentOverrides otherOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> otherAzOverrides = new HashMap<>();

    DeviceInfo masterDeviceInfo = new DeviceInfo();
    masterDeviceInfo.volumeSize = 100;
    masterDeviceInfo.numVolumes = 1;

    DeviceInfo tserverDeviceInfo = new DeviceInfo();
    tserverDeviceInfo.volumeSize = 200;
    tserverDeviceInfo.numVolumes = 2;

    DeviceInfo azLevelDeviceInfo = new DeviceInfo();
    azLevelDeviceInfo.volumeSize = 50;

    AZOverrides azOverride = new AZOverrides();
    azOverride.setDeviceInfo(azLevelDeviceInfo);

    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    PerProcessDetails masterDetails = new PerProcessDetails();
    masterDetails.setDeviceInfo(masterDeviceInfo);
    PerProcessDetails tserverDetails = new PerProcessDetails();
    tserverDetails.setDeviceInfo(tserverDeviceInfo);

    perProcess.put(ServerType.MASTER, masterDetails);
    perProcess.put(ServerType.TSERVER, tserverDetails);
    azOverride.setPerProcess(perProcess);

    otherAzOverrides.put(az1, azOverride);
    otherOverrides.setAzOverrides(otherAzOverrides);
    otherIntent.setUserIntentOverrides(otherOverrides);

    // Execute with isDedicatedMaster=true
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, null, true);

    // Verify: should use MASTER per-process device info
    UserIntentOverrides currentOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(currentOverrides);
    AZOverrides currentAz1 = currentOverrides.getAzOverrides().get(az1);
    assertNotNull(currentAz1);

    // AZ-level device info should be copied
    assertNotNull(currentAz1.getDeviceInfo());
    assertEquals(Integer.valueOf(50), currentAz1.getDeviceInfo().volumeSize);

    // Per-process MASTER device info should be copied
    assertNotNull(currentAz1.getPerProcess());
    assertNotNull(currentAz1.getPerProcess().get(ServerType.MASTER));
    DeviceInfo copiedMasterDeviceInfo =
        currentAz1.getPerProcess().get(ServerType.MASTER).getDeviceInfo();
    assertNotNull(copiedMasterDeviceInfo);
    assertEquals(Integer.valueOf(100), copiedMasterDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(1), copiedMasterDeviceInfo.numVolumes);

    // Assert tserver per-process is null
    assertNull(currentAz1.getPerProcess().get(ServerType.TSERVER));
  }

  @Test
  public void testUpdateAZVolumeOverrides_WithTServer() {
    // Test that isDedicatedMaster=false uses TSERVER server type
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);

    // Setup other UserIntent with per-process device info
    UserIntentOverrides otherOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> otherAzOverrides = new HashMap<>();

    DeviceInfo masterDeviceInfo = new DeviceInfo();
    masterDeviceInfo.volumeSize = 100;

    DeviceInfo tserverDeviceInfo = new DeviceInfo();
    tserverDeviceInfo.volumeSize = 200;
    tserverDeviceInfo.numVolumes = 2;

    AZOverrides azOverride = new AZOverrides();
    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    PerProcessDetails masterDetails = new PerProcessDetails();
    masterDetails.setDeviceInfo(masterDeviceInfo);
    PerProcessDetails tserverDetails = new PerProcessDetails();
    tserverDetails.setDeviceInfo(tserverDeviceInfo);

    perProcess.put(ServerType.MASTER, masterDetails);
    perProcess.put(ServerType.TSERVER, tserverDetails);
    azOverride.setPerProcess(perProcess);

    otherAzOverrides.put(az1, azOverride);
    otherOverrides.setAzOverrides(otherAzOverrides);
    otherIntent.setUserIntentOverrides(otherOverrides);

    // Execute with isDedicatedMaster=false
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, null, false);

    // Verify: should use TSERVER per-process device info
    UserIntentOverrides currentOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(currentOverrides);
    AZOverrides currentAz1 = currentOverrides.getAzOverrides().get(az1);
    assertNotNull(currentAz1);

    // Per-process TSERVER device info should be copied
    assertNotNull(currentAz1.getPerProcess());
    assertNotNull(currentAz1.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo copiedTserverDeviceInfo =
        currentAz1.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertNotNull(copiedTserverDeviceInfo);
    assertEquals(Integer.valueOf(200), copiedTserverDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(2), copiedTserverDeviceInfo.numVolumes);

    // per-process master should be null
    assertNull(currentAz1.getPerProcess().get(ServerType.MASTER));
  }

  @Test
  public void testUpdateAZVolumeOverrides_OtherIntentHasNoOverrides() {
    // Test when other UserIntent has no overrides - should unset current intent's overrides
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);

    // Setup currentIntent with initial overrides (AZ-level and per-process)
    UserIntentOverrides currentOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> currentAzOverrides = new HashMap<>();

    DeviceInfo currentDeviceInfo = new DeviceInfo();
    currentDeviceInfo.volumeSize = 100;
    currentDeviceInfo.numVolumes = 1;

    DeviceInfo currentTserverDeviceInfo = new DeviceInfo();
    currentTserverDeviceInfo.volumeSize = 200;
    currentTserverDeviceInfo.numVolumes = 2;

    AZOverrides currentAzOverride = new AZOverrides();
    currentAzOverride.setDeviceInfo(currentDeviceInfo);

    Map<ServerType, PerProcessDetails> currentPerProcess = new HashMap<>();
    PerProcessDetails currentTserverDetails = new PerProcessDetails();
    currentTserverDetails.setDeviceInfo(currentTserverDeviceInfo);
    currentPerProcess.put(ServerType.TSERVER, currentTserverDetails);
    currentAzOverride.setPerProcess(currentPerProcess);

    currentAzOverrides.put(az1, currentAzOverride);
    currentOverrides.setAzOverrides(currentAzOverrides);
    currentIntent.setUserIntentOverrides(currentOverrides);

    // Verify initial state
    assertNotNull(currentIntent.getUserIntentOverrides());
    assertNotNull(currentIntent.getUserIntentOverrides().getAzOverrides().get(az1));
    assertNotNull(currentIntent.getUserIntentOverrides().getAzOverrides().get(az1).getDeviceInfo());
    assertEquals(
        Integer.valueOf(100),
        currentIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1)
            .getDeviceInfo()
            .volumeSize);
    assertNotNull(
        currentIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1)
            .getPerProcess()
            .get(ServerType.TSERVER));
    assertEquals(
        Integer.valueOf(200),
        currentIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1)
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo()
            .volumeSize);

    // otherIntent has no overrides
    otherIntent.setUserIntentOverrides(null);

    // Execute
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, null, false);

    // Verify: current intent's overrides should be unset (deviceInfo and per-process deviceInfo
    // should be null)
    UserIntentOverrides updatedOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(updatedOverrides);
    assertNull(updatedOverrides.getAzOverrides());
  }

  @Test
  public void testUpdateAZVolumeOverrides_CurrentIntentHasNoOverrides() {
    // Test when current UserIntent has no overrides initially
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);

    // Setup other UserIntent with device info
    UserIntentOverrides otherOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> otherAzOverrides = new HashMap<>();

    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.volumeSize = 100;

    AZOverrides azOverride = new AZOverrides();
    azOverride.setDeviceInfo(deviceInfo);
    otherAzOverrides.put(az1, azOverride);
    otherOverrides.setAzOverrides(otherAzOverrides);
    otherIntent.setUserIntentOverrides(otherOverrides);

    // currentIntent has no overrides
    currentIntent.setUserIntentOverrides(null);

    // Execute
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, null, false);

    // Verify: current intent should now have overrides
    UserIntentOverrides currentOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(currentOverrides);
    assertNotNull(currentOverrides.getAzOverrides());
    assertEquals(1, currentOverrides.getAzOverrides().size());

    AZOverrides currentAz1 = currentOverrides.getAzOverrides().get(az1);
    assertNotNull(currentAz1);
    assertNotNull(currentAz1.getDeviceInfo());
    assertEquals(Integer.valueOf(100), currentAz1.getDeviceInfo().volumeSize);
  }

  @Test
  public void testUpdateAZVolumeOverrides_OnlyPerProcessDeviceInfo() {
    // Test when only per-process device info exists (no AZ-level device info)
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);

    // Setup other UserIntent with only per-process device info
    UserIntentOverrides otherOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> otherAzOverrides = new HashMap<>();

    DeviceInfo tserverDeviceInfo = new DeviceInfo();
    tserverDeviceInfo.volumeSize = 200;

    AZOverrides azOverride = new AZOverrides();
    // No AZ-level device info
    azOverride.setDeviceInfo(null);

    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    PerProcessDetails tserverDetails = new PerProcessDetails();
    tserverDetails.setDeviceInfo(tserverDeviceInfo);
    perProcess.put(ServerType.TSERVER, tserverDetails);
    azOverride.setPerProcess(perProcess);

    otherAzOverrides.put(az1, azOverride);
    otherOverrides.setAzOverrides(otherAzOverrides);
    otherIntent.setUserIntentOverrides(otherOverrides);

    // Execute
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, null, false);

    // Verify: per-process device info should be copied, AZ-level should be null
    UserIntentOverrides currentOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(currentOverrides);
    AZOverrides currentAz1 = currentOverrides.getAzOverrides().get(az1);
    assertNotNull(currentAz1);

    // AZ-level device info should be null
    assertNull(currentAz1.getDeviceInfo());

    // Per-process TSERVER device info should be copied
    assertNotNull(currentAz1.getPerProcess());
    assertNotNull(currentAz1.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo copiedTserverDeviceInfo =
        currentAz1.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertNotNull(copiedTserverDeviceInfo);
    assertEquals(Integer.valueOf(200), copiedTserverDeviceInfo.volumeSize);
  }

  @Test
  public void testUpdateAZVolumeOverrides_MultipleAZsWithMixedConfig() {
    // Test multiple AZs with different configurations
    UserIntent currentIntent = new UserIntent();
    UserIntent otherIntent = new UserIntent();

    UUID az1 = UUID.randomUUID();
    UUID az2 = UUID.randomUUID();
    UUID az3 = UUID.randomUUID();
    Set<UUID> azs = new HashSet<>();
    azs.add(az1);
    azs.add(az2);
    azs.add(az3);

    // Setup other UserIntent with different configs for each AZ
    UserIntentOverrides otherOverrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> otherAzOverrides = new HashMap<>();

    // AZ1: Only AZ-level device info
    DeviceInfo deviceInfo1 = new DeviceInfo();
    deviceInfo1.volumeSize = 100;
    AZOverrides azOverride1 = new AZOverrides();
    azOverride1.setDeviceInfo(deviceInfo1);

    // AZ2: Only per-process device info
    DeviceInfo tserverDeviceInfo2 = new DeviceInfo();
    tserverDeviceInfo2.volumeSize = 200;
    AZOverrides azOverride2 = new AZOverrides();
    Map<ServerType, PerProcessDetails> perProcess2 = new HashMap<>();
    PerProcessDetails tserverDetails2 = new PerProcessDetails();
    tserverDetails2.setDeviceInfo(tserverDeviceInfo2);
    perProcess2.put(ServerType.TSERVER, tserverDetails2);
    azOverride2.setPerProcess(perProcess2);

    // AZ3: Both AZ-level and per-process device info
    DeviceInfo deviceInfo3 = new DeviceInfo();
    deviceInfo3.volumeSize = 300;
    DeviceInfo tserverDeviceInfo3 = new DeviceInfo();
    tserverDeviceInfo3.volumeSize = 350;
    AZOverrides azOverride3 = new AZOverrides();
    azOverride3.setDeviceInfo(deviceInfo3);
    Map<ServerType, PerProcessDetails> perProcess3 = new HashMap<>();
    PerProcessDetails tserverDetails3 = new PerProcessDetails();
    tserverDetails3.setDeviceInfo(tserverDeviceInfo3);
    perProcess3.put(ServerType.TSERVER, tserverDetails3);
    azOverride3.setPerProcess(perProcess3);

    otherAzOverrides.put(az1, azOverride1);
    otherAzOverrides.put(az2, azOverride2);
    otherAzOverrides.put(az3, azOverride3);
    otherOverrides.setAzOverrides(otherAzOverrides);
    otherIntent.setUserIntentOverrides(otherOverrides);

    // Execute
    currentIntent.updateAZVolumeOverrides(otherIntent, azs, null, false);

    // Verify all three AZs
    UserIntentOverrides currentOverrides = currentIntent.getUserIntentOverrides();
    assertNotNull(currentOverrides);
    assertEquals(3, currentOverrides.getAzOverrides().size());

    // AZ1: Only AZ-level
    AZOverrides currentAz1 = currentOverrides.getAzOverrides().get(az1);
    assertNotNull(currentAz1);
    assertNotNull(currentAz1.getDeviceInfo());
    assertEquals(Integer.valueOf(100), currentAz1.getDeviceInfo().volumeSize);
    assertNull(currentAz1.getPerProcess());

    // AZ2: Only per-process
    AZOverrides currentAz2 = currentOverrides.getAzOverrides().get(az2);
    assertNotNull(currentAz2);
    assertNull(currentAz2.getDeviceInfo());
    assertNotNull(currentAz2.getPerProcess());
    assertNotNull(currentAz2.getPerProcess().get(ServerType.TSERVER));
    assertEquals(
        Integer.valueOf(200),
        currentAz2.getPerProcess().get(ServerType.TSERVER).getDeviceInfo().volumeSize);

    // AZ3: Both
    AZOverrides currentAz3 = currentOverrides.getAzOverrides().get(az3);
    assertNotNull(currentAz3);
    assertNotNull(currentAz3.getDeviceInfo());
    assertEquals(Integer.valueOf(300), currentAz3.getDeviceInfo().volumeSize);
    assertNotNull(currentAz3.getPerProcess());
    assertNotNull(currentAz3.getPerProcess().get(ServerType.TSERVER));
    assertEquals(
        Integer.valueOf(350),
        currentAz3.getPerProcess().get(ServerType.TSERVER).getDeviceInfo().volumeSize);
  }

  @Test
  public void testUnsetCgroupSize_ClearsCgroupSizeFromAZOverrides() {
    // Test that cgroupSize is cleared from AZ overrides when other fields exist
    UserIntentOverrides overrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> azOverrides = new HashMap<>();

    UUID az1 = UUID.randomUUID();
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.volumeSize = 100;

    AZOverrides azOverride = new AZOverrides();
    azOverride.setCgroupSize(1024);
    azOverride.setDeviceInfo(deviceInfo);

    azOverrides.put(az1, azOverride);
    overrides.setAzOverrides(azOverrides);

    // Verify initial state
    assertEquals(Integer.valueOf(1024), overrides.getAzOverrides().get(az1).getCgroupSize());

    // Execute
    overrides.unsetCgroupSize();

    // Verify: cgroupSize should be cleared, but other fields should remain
    assertNotNull(overrides.getAzOverrides());
    AZOverrides updatedAzOverride = overrides.getAzOverrides().get(az1);
    assertNotNull(updatedAzOverride);
    assertNull(updatedAzOverride.getCgroupSize());
    assertNotNull(updatedAzOverride.getDeviceInfo());
    assertEquals(Integer.valueOf(100), updatedAzOverride.getDeviceInfo().volumeSize);
  }

  @Test
  public void testUnsetCgroupSize_RemovesOverridesWhenCgroupSizeWasOnlyValue() {
    // Test that azOverrides becomes null when cgroupSize was the only value set
    UserIntentOverrides overrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> azOverrides = new HashMap<>();

    UUID az1 = UUID.randomUUID();
    AZOverrides azOverride = new AZOverrides();
    azOverride.setCgroupSize(2048);

    azOverrides.put(az1, azOverride);
    overrides.setAzOverrides(azOverrides);

    // Verify initial state
    assertNotNull(overrides.getAzOverrides());
    assertEquals(Integer.valueOf(2048), overrides.getAzOverrides().get(az1).getCgroupSize());

    // Execute
    overrides.unsetCgroupSize();

    // Verify: azOverrides should be null since cgroupSize was the only value
    assertNull(overrides.getAzOverrides());
  }
}
