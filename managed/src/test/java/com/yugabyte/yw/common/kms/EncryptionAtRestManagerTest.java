package com.yugabyte.yw.common.kms;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.algorithms.SupportedAlgorithmInterface;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.KmsHistoryId.TargetType;
import com.yugabyte.yw.models.Universe;
import java.util.Base64;
import java.util.List;
import java.util.Random;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestManagerTest extends FakeDBApplication {
  @Mock EncryptionAtRestService<? extends SupportedAlgorithmInterface> mockEARService;
  @InjectMocks EncryptionAtRestManager testManager1;
  EncryptionAtRestManager testManager2 = Mockito.spy(new EncryptionAtRestManager(null));
  KmsConfig kmsConfig1;
  KmsConfig kmsConfig2;
  KmsConfig kmsConfig3;

  byte[] universeKey1 = getRandomBytes();
  byte[] universeKey2 = getRandomBytes();

  byte[] universeKeyRef1 = getRandomBytes();
  byte[] universeKeyRef2 = getRandomBytes();
  byte[] universeKeyRef3 = getRandomBytes();
  byte[] universeKeyRef4 = getRandomBytes();

  Customer testCustomer;
  Universe testUniverse;
  EncryptionAtRestConfig keyConfig;

  public byte[] getRandomBytes() {
    byte[] randomBytes = new byte[32];
    new Random().nextBytes(randomBytes);
    return randomBytes;
  }

  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse();
    keyConfig = new EncryptionAtRestConfig();
    kmsConfig1 =
        KmsConfig.createKMSConfig(
            testCustomer.getUuid(),
            KeyProvider.AWS,
            new ObjectMapper().createObjectNode(),
            "kms-config-1");
    kmsConfig2 =
        KmsConfig.createKMSConfig(
            testCustomer.getUuid(),
            KeyProvider.AZU,
            new ObjectMapper().createObjectNode(),
            "kms-config-2");
    kmsConfig3 =
        KmsConfig.createKMSConfig(
            testCustomer.getUuid(),
            KeyProvider.GCP,
            new ObjectMapper().createObjectNode(),
            "kms-config-3");
    doReturn(universeKeyRef1)
        .when(mockEARService)
        .createKey(testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), keyConfig);
    doReturn(universeKeyRef2)
        .when(mockEARService)
        .rotateKey(testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), keyConfig);
    when(mockEARService.retrieveKey(any(), any(), any(byte[].class))).thenCallRealMethod();
    doReturn(mockEARService).when(testManager2).getServiceInstance(anyString());
  }

  @Test
  public void testGetServiceInstanceKeyProviderDoesNotExist() {
    EncryptionAtRestService<? extends SupportedAlgorithmInterface> keyService =
        testManager1.getServiceInstance("NONSENSE");
    assertNull(keyService);
  }

  @Test
  public void testGetServiceInstance() {
    EncryptionAtRestService<? extends SupportedAlgorithmInterface> keyService =
        testManager1.getServiceInstance("AWS");
    assertNotNull(keyService);
  }

  @Test
  public void testGenerateUniverseKeyCreateUniverseKey() {
    // Ensure the universe has no existing KMS history.
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 0);

    // Create a universe key.
    byte[] universeKeyData =
        testManager2.generateUniverseKey(
            kmsConfig1.getConfigUUID(), testUniverse.getUniverseUUID(), keyConfig);
    assertEquals(universeKeyRef1, universeKeyData);

    // After rotating the universe key, there should be 1 entry in the KMS history table.
    List<KmsHistory> kmsHistoryList =
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID());
    assertEquals(1, kmsHistoryList.size());

    // Verify the newly created universe key.
    assertEquals(
        Base64.getEncoder().encodeToString(universeKeyRef1),
        kmsHistoryList.get(0).getUuid().keyRef);
    assertEquals(0, kmsHistoryList.get(0).getUuid().reEncryptionCount);
  }

  @Test
  public void testGenerateUniverseKeyRotateUniverseKey() {
    // Add a universe key already so it rotates universe key instead of creating.
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 0);
    EncryptionAtRestUtil.addKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    EncryptionAtRestUtil.activateKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 1);

    // Rotate the universe key.
    byte[] universeKeyData =
        testManager2.generateUniverseKey(
            kmsConfig1.getConfigUUID(), testUniverse.getUniverseUUID(), keyConfig);
    assertEquals(universeKeyRef2, universeKeyData);

    // After rotating the universe key, there should be 2 entries in the KMS history table.
    List<KmsHistory> kmsHistoryList =
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID());
    assertEquals(2, kmsHistoryList.size());

    // Verify the newly rotated universe key.
    assertEquals(
        Base64.getEncoder().encodeToString(universeKeyRef2),
        kmsHistoryList.get(0).getUuid().keyRef);
    assertEquals(0, kmsHistoryList.get(0).getUuid().reEncryptionCount);

    // Verify the previously existing universe key.
    assertEquals(
        Base64.getEncoder().encodeToString(universeKeyRef1),
        kmsHistoryList.get(1).getUuid().keyRef);
    assertEquals(0, kmsHistoryList.get(1).getUuid().reEncryptionCount);
  }

  @Test
  public void testReEncryptActiveUniverseKeysRotateSameMasterKey() {
    doReturn(universeKey1).when(mockEARService).retrieveKeyWithService(any(), eq(universeKeyRef1));
    doReturn(universeKeyRef1).when(mockEARService).encryptKeyWithService(any(), eq(universeKey1));

    // Add a universe key so we can rotate master key.
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 0);
    EncryptionAtRestUtil.addKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    EncryptionAtRestUtil.activateKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 1);

    // Rotate master key from kmsConfig1 to kmsConfig2.
    testManager2.reEncryptActiveUniverseKeys(
        testUniverse.getUniverseUUID(), kmsConfig2.getConfigUUID());

    // After rotating the master key, there should be 1 active entry in the
    // KMS history table with new master key.
    List<KmsHistory> activeKmsHistoryList =
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID());
    assertEquals(1, activeKmsHistoryList.size());

    // After rotating the master key, there should be 2 total entries in the
    // KMS history table for the universe.
    List<KmsHistory> allKmsHistoryList =
        KmsHistory.getAllTargetKeyRefs(
            testUniverse.getUniverseUUID(), KmsHistoryId.TargetType.UNIVERSE_KEY);
    assertEquals(2, allKmsHistoryList.size());

    // Verify the newly re-encrypted universe key.
    assertEquals(1, allKmsHistoryList.get(0).getUuid().reEncryptionCount);
    assertEquals(
        Base64.getEncoder().encodeToString(universeKeyRef1),
        allKmsHistoryList.get(0).getUuid().keyRef);
    assertTrue(allKmsHistoryList.get(0).isActive());
  }

  @Test
  public void testReEncryptActiveUniverseKeysRotateDifferentMasterKey() {
    doReturn(universeKey1).when(mockEARService).retrieveKeyWithService(any(), eq(universeKeyRef1));
    doReturn(universeKeyRef2).when(mockEARService).encryptKeyWithService(any(), eq(universeKey1));

    // Add a universe key so we can rotate master key.
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 0);
    EncryptionAtRestUtil.addKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    EncryptionAtRestUtil.activateKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 1);

    // Rotate master key from kmsConfig1 to kmsConfig2.
    testManager2.reEncryptActiveUniverseKeys(
        testUniverse.getUniverseUUID(), kmsConfig2.getConfigUUID());

    // After rotating the master key, there should be 1 active entry in the
    // KMS history table with new master key.
    List<KmsHistory> activeKmsHistoryList =
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID());
    assertEquals(1, activeKmsHistoryList.size());

    // After rotating the master key, there should be 2 total entries in the
    // KMS history table for the universe.
    List<KmsHistory> allKmsHistoryList =
        KmsHistory.getAllTargetKeyRefs(
            testUniverse.getUniverseUUID(), KmsHistoryId.TargetType.UNIVERSE_KEY);
    assertEquals(2, allKmsHistoryList.size());

    // Verify the newly re-encrypted universe key.
    assertEquals(1, allKmsHistoryList.get(0).getUuid().reEncryptionCount);
    assertEquals(
        Base64.getEncoder().encodeToString(universeKeyRef2),
        allKmsHistoryList.get(0).getUuid().keyRef);
    assertTrue(allKmsHistoryList.get(0).isActive());
  }

  @Test
  public void testRotateMasterKeyWithPreviouslyRotatedKmsConfig() {
    doReturn(universeKey1).when(mockEARService).retrieveKeyWithService(any(), eq(universeKeyRef1));
    doReturn(universeKey2).when(mockEARService).retrieveKeyWithService(any(), eq(universeKeyRef2));
    doReturn(universeKeyRef3).when(mockEARService).encryptKeyWithService(any(), eq(universeKey1));
    doReturn(universeKeyRef4).when(mockEARService).encryptKeyWithService(any(), eq(universeKey2));

    // Add 2 universe keys with different KMS configs so we can rotate master key.
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 0);
    EncryptionAtRestUtil.addKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    EncryptionAtRestUtil.activateKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig1.getConfigUUID(), universeKeyRef1);
    KmsHistory.createKmsHistory(
            kmsConfig2.getConfigUUID(),
            testUniverse.getUniverseUUID(),
            TargetType.UNIVERSE_KEY,
            Base64.getEncoder().encodeToString(universeKeyRef2),
            0,
            Base64.getEncoder().encodeToString(universeKeyRef2))
        .save();
    EncryptionAtRestUtil.activateKeyRef(
        testUniverse.getUniverseUUID(), kmsConfig2.getConfigUUID(), universeKeyRef2);
    assertEquals(
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID()).size(), 2);

    // After rotating the master key, there should be 2 total entries in the
    // KMS history table for the universe.
    List<KmsHistory> allKmsHistoryList =
        KmsHistory.getAllTargetKeyRefs(
            testUniverse.getUniverseUUID(), KmsHistoryId.TargetType.UNIVERSE_KEY);
    assertEquals(2, allKmsHistoryList.size());

    // Rotate master key from kmsConfig2 to kmsConfig3.
    testManager2.reEncryptActiveUniverseKeys(
        testUniverse.getUniverseUUID(), kmsConfig3.getConfigUUID());

    // After rotating the master key, there should be 2 active entries in the
    // KMS history table with new master key.
    List<KmsHistory> activeKmsHistoryList =
        KmsHistory.getAllUniverseKeysWithActiveMasterKey(testUniverse.getUniverseUUID());
    assertEquals(2, activeKmsHistoryList.size());

    // After rotating the master key, there should be 4 total entries in the
    // KMS history table for the universe.
    allKmsHistoryList =
        KmsHistory.getAllTargetKeyRefs(
            testUniverse.getUniverseUUID(), KmsHistoryId.TargetType.UNIVERSE_KEY);
    assertEquals(4, allKmsHistoryList.size());

    // Verify the newly re-encrypted universe key 2.
    System.out.println(allKmsHistoryList);
    assertEquals(1, allKmsHistoryList.get(0).getUuid().reEncryptionCount);
    assertEquals(
        Base64.getEncoder().encodeToString(universeKeyRef4),
        allKmsHistoryList.get(0).getUuid().keyRef);
    assertTrue(allKmsHistoryList.get(0).isActive());

    // Verify the newly re-encrypted universe key 1.
    assertEquals(1, allKmsHistoryList.get(1).getUuid().reEncryptionCount);
    assertEquals(
        Base64.getEncoder().encodeToString(universeKeyRef3),
        allKmsHistoryList.get(1).getUuid().keyRef);
    assertFalse(allKmsHistoryList.get(1).isActive());
  }
}
