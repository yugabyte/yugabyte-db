package com.yugabyte.yw.common;

import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mockStatic;

import java.util.Date;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

import junitparams.JUnitParamsRunner;

import org.junit.Test;
import org.apache.commons.lang3.time.DateUtils;
import org.junit.Before;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.mockito.junit.MockitoJUnitRunner;
import org.mockito.MockedStatic;

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKeyId;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.AccessKey.KeyInfo;

import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import java.util.List;

import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.typesafe.config.Config;

@RunWith(JUnitParamsRunner.class)
public class AccessKeyRotationUtilTest extends FakeDBApplication {

  @InjectMocks AccessKeyRotationUtil accessKeyRotationUtil;
  @Mock Config mockRuntimeConfig;
  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;
  @Mock Config mockConfigUniverseScope;

  private Provider defaultProvider;
  private Region defaultRegion;
  private Customer defaultCustomer;
  private AccessKey defaultAccessKey;

  @Before
  public void setup() {
    MockitoAnnotations.initMocks(this);
    when(mockRuntimeConfigFactory.forUniverse(any())).thenReturn(mockConfigUniverseScope);
    when(mockConfigUniverseScope.getInt(AccessKeyRotationUtil.SSH_KEY_EXPIRATION_THRESHOLD_DAYS))
        .thenReturn(365);
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultRegion = Region.create(defaultProvider, "us-west-2", "US West 2", "yb-image");
    AccessKey.KeyInfo defaultKeyInfo = new AccessKey.KeyInfo();
    defaultKeyInfo.sshUser = "ssh_user";
    defaultKeyInfo.sshPort = 22;
    defaultAccessKey = AccessKey.create(defaultProvider.uuid, "default-key", defaultKeyInfo);
  }

  @Test
  public void testFailManuallyProvisioned() {
    Provider onpremProvider = ModelFactory.newProvider(defaultCustomer, CloudType.onprem);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.skipProvisioning = true;
    AccessKey accessKey = AccessKey.create(onpremProvider.uuid, "key-code-1", keyInfo);
    // provider has manually provisioned nodes
    assertThrows(
        PlatformServiceException.class,
        () ->
            accessKeyRotationUtil.failManuallyProvisioned(
                onpremProvider.uuid, defaultAccessKey.getKeyCode()));
    // new key has skip provisioining set to true
    assertThrows(
        PlatformServiceException.class,
        () ->
            accessKeyRotationUtil.failManuallyProvisioned(
                defaultProvider.uuid, accessKey.getKeyCode()));
  }

  @Test
  public void testCreateAccessKeyForProvider() {
    AccessKey.KeyInfo keyInfo = defaultAccessKey.getKeyInfo();
    List<AccessKey> accessKeys = new ArrayList<AccessKey>();
    accessKeys.add(defaultAccessKey);
    MockedStatic<AccessKey> mockAccessKey = mockStatic(AccessKey.class);
    mockAccessKey.when(() -> AccessKey.getAll(eq(defaultProvider.uuid))).thenReturn(accessKeys);
    mockAccessKey
        .when(() -> AccessKey.getNewKeyCode(eq(defaultProvider)))
        .thenReturn(defaultAccessKey.getKeyCode());
    mockAccessKey
        .when(() -> AccessKey.getLatestKey(eq(defaultProvider.uuid)))
        .thenReturn(defaultAccessKey);
    when(mockAccessManager.addKey(
            any(UUID.class),
            eq(defaultAccessKey.getKeyCode()),
            any(),
            eq(keyInfo.sshUser),
            eq(keyInfo.sshPort),
            eq(keyInfo.airGapInstall),
            eq(keyInfo.skipProvisioning),
            eq(keyInfo.setUpChrony),
            eq(keyInfo.ntpServers),
            eq(keyInfo.showSetUpChrony)))
        .thenReturn(defaultAccessKey);
    UUID providerUUID = defaultProvider.uuid;
    AccessKey accessKey =
        accessKeyRotationUtil.createAccessKeyForProvider(defaultCustomer.uuid, providerUUID);
    int numRegions = Region.getByProvider(providerUUID).size();
    assertNotNull(accessKey);
    assertTrue(accessKey.getKeyCode().equals(defaultAccessKey.getKeyCode()));
    verify(mockAccessManager, times(numRegions))
        .addKey(
            any(UUID.class),
            eq(defaultAccessKey.getKeyCode()),
            any(),
            eq(keyInfo.sshUser),
            eq(keyInfo.sshPort),
            eq(keyInfo.airGapInstall),
            eq(keyInfo.skipProvisioning),
            eq(keyInfo.setUpChrony),
            eq(keyInfo.ntpServers),
            eq(keyInfo.showSetUpChrony));
    mockAccessKey.close();
  }

  @Test
  public void testRemoveDeletedUniverses() {
    List<UUID> universeUUIDs = new ArrayList<UUID>();
    Universe uni1 = ModelFactory.createUniverse("uni1");
    Universe uni2 = ModelFactory.createUniverse("uni2");
    universeUUIDs.add(uni1.universeUUID);
    universeUUIDs.add(uni2.universeUUID);
    Universe.delete(uni2.universeUUID);
    Set<UUID> filteredUniverses =
        accessKeyRotationUtil
            .removeDeletedUniverses(universeUUIDs)
            .stream()
            .collect(Collectors.toSet());
    assertTrue(filteredUniverses.contains(uni1.universeUUID));
    assertFalse(filteredUniverses.contains(uni2.universeUUID));
  }

  @Test
  public void testRemovePausedUniverses() {
    List<UUID> universeUUIDs = new ArrayList<UUID>();
    Universe uni1 = ModelFactory.createUniverse("uni1");
    Universe uni2 = ModelFactory.createUniverse("uni2");
    universeUUIDs.add(uni1.universeUUID);
    universeUUIDs.add(uni2.universeUUID);
    setUniversePaused(true, uni2);
    Set<UUID> filteredUniverses =
        accessKeyRotationUtil
            .removePausedUniverses(universeUUIDs)
            .stream()
            .collect(Collectors.toSet());
    assertTrue(filteredUniverses.contains(uni1.universeUUID));
    assertFalse(filteredUniverses.contains(uni2.universeUUID));
  }

  @Test
  public void testGetSSHKeyExpiryDaysDisabled() {
    when(mockConfigUniverseScope.getBoolean(AccessKeyRotationUtil.SSH_KEY_EXPIRATION_ENABLED))
        .thenReturn(false);
    Universe uni = ModelFactory.createUniverse("uni1");
    setUniverseAccessKey(defaultAccessKey.getKeyCode(), uni);
    Map<AccessKeyId, AccessKey> allAccessKeys = accessKeyRotationUtil.createAllAccessKeysMap();
    Double daysToExpiry = accessKeyRotationUtil.getSSHKeyExpiryDays(uni, allAccessKeys);
    assertNull(daysToExpiry);
  }

  @Test
  public void testGetSSHKeyExpiryDays() {
    when(mockConfigUniverseScope.getBoolean(AccessKeyRotationUtil.SSH_KEY_EXPIRATION_ENABLED))
        .thenReturn(true);
    Universe uni = ModelFactory.createUniverse("uni1");
    setUniverseAccessKey(defaultAccessKey.getKeyCode(), uni);
    Map<AccessKeyId, AccessKey> allAccessKeys = accessKeyRotationUtil.createAllAccessKeysMap();
    long currentTime = System.currentTimeMillis();
    Double daysToExpiry = accessKeyRotationUtil.getSSHKeyExpiryDays(uni, allAccessKeys);
    Date expirationDate = DateUtils.addDays(defaultAccessKey.getCreationDate(), 365);
    Double expectedDaysToExpiry =
        (double) TimeUnit.MILLISECONDS.toDays(expirationDate.getTime() - currentTime);
    assertNotNull(daysToExpiry);
    assertTrue(daysToExpiry.equals(expectedDaysToExpiry));
  }

  @Test
  public void testGetUniverseAccessKeys() {
    List<AccessKey> expectedAccessKeys = new ArrayList<AccessKey>();
    expectedAccessKeys.add(defaultAccessKey);
    Universe uni = ModelFactory.createUniverse("uni1");
    setUniverseAccessKey(defaultAccessKey.getKeyCode(), uni);
    Map<AccessKeyId, AccessKey> allAccessKeys = accessKeyRotationUtil.createAllAccessKeysMap();
    List<AccessKey> universeAccessKeys =
        accessKeyRotationUtil.getUniverseAccessKeys(uni, allAccessKeys);
    assertTrue(universeAccessKeys.size() == 1);
    assertEquals(expectedAccessKeys, universeAccessKeys);
  }

  public static void setUniversePaused(boolean value, Universe universe) {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.universePaused = value;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universe.universeUUID, updater);
  }

  public static void setUniverseAccessKey(String accessKeyCode, Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().clusters.get(0).userIntent;
    userIntent.accessKeyCode = accessKeyCode;
    Universe.saveDetails(
        universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent, false /* setMasters */));
  }
}
