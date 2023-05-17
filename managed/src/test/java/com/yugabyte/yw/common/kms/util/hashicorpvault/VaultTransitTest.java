/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.kms.util.hashicorpvault;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockingDetails;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultSecretEngineBase.KMSEngineType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultSecretEngineBase.VaultOperations;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class VaultTransitTest extends FakeDBApplication {
  protected static final Logger LOG = LoggerFactory.getLogger(VaultTransitTest.class);

  // Create fake auth config details
  public ObjectMapper mapper = new ObjectMapper();
  public ObjectNode fakeAuthConfig = mapper.createObjectNode();

  boolean MOCK_RUN;

  String vaultAddr;
  String vaultToken;
  String sEngine;
  String mountPath;

  @Before
  public void setUp() {

    MOCK_RUN = VaultEARServiceUtilTest.MOCK_RUN;
    vaultAddr = VaultEARServiceUtilTest.vaultAddr;
    vaultToken = VaultEARServiceUtilTest.vaultToken;
    sEngine = VaultEARServiceUtilTest.sEngine;
    mountPath = VaultEARServiceUtilTest.mountPath;

    fakeAuthConfig = (ObjectNode) Json.parse(VaultEARServiceUtilTest.jsonString);
  }

  @Test
  public void testBuildPath() {

    String validPath, path;

    validPath = "transit/encrypt/key1";
    path =
        VaultSecretEngineBase.buildPath(
            KMSEngineType.TRANSIT.toString() + "/", VaultOperations.ENCRYPT, "key1");
    assertEquals(validPath, path);

    validPath = "transit/decrypt/key2";
    path =
        KMSEngineType.TRANSIT.toString() + "/" + VaultOperations.DECRYPT.toString() + "/" + "key2";
    assertEquals(validPath, path);
  }

  @Test
  public void testEncryptWithVault() throws Exception {
    String key = "key1";
    byte[] data = "TestData".getBytes();

    VaultAccessor vAccessor;
    VaultTransit transitEngine;

    if (MOCK_RUN) {
      vAccessor = mock(VaultAccessor.class);
      String rdata = "ENCRYPTED_TestData";

      when(vAccessor.getMountType("qa_transit/")).thenReturn("TRANSIT");
      when(vAccessor.listAt(anyString())).thenReturn(key);
      when(vAccessor.writeAt(anyString(), any(), anyString()))
          .thenReturn(rdata) // for encryption
          .thenReturn(Base64.getEncoder().encodeToString(data)) // for decryption
      ;
    } else {
      vAccessor = VaultAccessor.buildVaultAccessor(vaultAddr, vaultToken);
    }
    transitEngine = new VaultTransit(vAccessor, mountPath, KeyType.CMK);
    byte[] eData = transitEngine.encryptString(key, data);
    byte[] dData = transitEngine.decryptString(key, eData);
    LOG.debug(
        "After ops vaules are: {} , {} ",
        new String(data, StandardCharsets.UTF_8),
        new String(dData, StandardCharsets.UTF_8));
    assertEquals(
        new String(data, StandardCharsets.UTF_8), new String(dData, StandardCharsets.UTF_8));

    if (MOCK_RUN) {
      verify(vAccessor, times(1)).listAt(anyString());
      verify(vAccessor, times(2)).writeAt(anyString(), any(), anyString());
    }
  }

  @Test
  public void testCreateAndDeleteNewKey() throws Exception {
    UUID universeUUID = UUID.randomUUID();
    UUID configUUID = UUID.randomUUID();
    String keyName = "key_yugabyte"; // HC_VAULT_EKE_NAME

    VaultAccessor vAccessor;
    VaultTransit transitEngine;

    if (MOCK_RUN) {
      vAccessor = mock(VaultAccessor.class);

      // for VaultTransit constructor
      when(vAccessor.getMountType("qa_transit/")).thenReturn("TRANSIT");
      // for VaultTransit.checkForPermissions
      when(vAccessor.listAt(anyString())).thenReturn(keyName);

      when(vAccessor.readAt(any(String.class), any(String.class)))
          .thenReturn("") // for createNewKeyWithEngine
          .thenReturn("1") // for createNewKeyWithEngine
          .thenReturn("1") // for local call
      ;
      when(vAccessor.writeAt(anyString(), any(), anyString()))
          .thenReturn("") // for createNewKeyWithEngine
          .thenReturn("") // for deletekey
      ;
      when(vAccessor.deleteAt(anyString())).thenReturn("");
    } else {
      vAccessor = VaultAccessor.buildVaultAccessor(vaultAddr, vaultToken);
    }

    transitEngine = new VaultTransit(vAccessor, mountPath, KeyType.CMK);

    String returnedName = HashicorpEARServiceUtil.getVaultKeyForUniverse(fakeAuthConfig);
    assertEquals(keyName, returnedName);

    boolean created = transitEngine.createNewKeyWithEngine(returnedName);
    assertTrue(created);

    {
      String path = transitEngine.buildPath(VaultOperations.KEYS, keyName);
      String result = vAccessor.readAt(path, "latest_version");
      LOG.debug("latest version of key is {}", result);
      assertNotEquals("", result);
    }

    transitEngine.deleteKey(keyName);

    if (MOCK_RUN) {
      verify(vAccessor, times(1)).listAt(anyString());
      verify(vAccessor, times(3)).readAt(anyString(), anyString());
      verify(vAccessor, times(2)).writeAt(anyString(), any(), anyString());
    }
  }

  @Test
  // @Ignore
  public void testReWrapString() throws Exception {

    String key = "key1";
    byte[] data = "TestData".getBytes();

    byte[] rbytes;

    VaultAccessor vAccessor;
    VaultTransit transitEngine;

    if (MOCK_RUN) {
      vAccessor = mock(VaultAccessor.class);

      // for VaultTransit constructor
      when(vAccessor.getMountType("qa_transit/")).thenReturn("TRANSIT");
      // for VaultTransit.checkForPermissions
      when(vAccessor.listAt(anyString())).thenReturn(key);

      when(vAccessor.readAt(any(String.class), any(String.class)))
          .thenReturn("") // for createNewKeyWithEngine
          .thenReturn("1") // for createNewKeyWithEngine
      ;
      String mock_data1 = "ENCRYPTED_TestData1";
      String mock_data2 = "ENCRYPTED_TestData2";
      when(vAccessor.writeAt(anyString(), any(), anyString()))
          .thenReturn(mock_data1) // for encryption
          .thenReturn(mock_data2) // for rewrap
          .thenReturn(Base64.getEncoder().encodeToString(data)) // for rewrap
      ;
      when(vAccessor.deleteAt(anyString())).thenReturn("");

      when(vAccessor.readAt(any(String.class), any(String.class))).thenReturn("1");
      Map<byte[], byte[]> result = new HashMap<byte[], byte[]>();
      result.put("ENCRYPTED1_TestData".getBytes(), "ENCRYPTED2_TestData".getBytes());

    } else {
      vAccessor = VaultAccessor.buildVaultAccessor(vaultAddr, vaultToken);
    }

    transitEngine = new VaultTransit(vAccessor, mountPath, KeyType.CMK);
    byte[] eData = transitEngine.encryptString(key, data);
    assertNotEquals(0, eData.length);

    ArrayList<byte[]> keyList = new ArrayList<byte[]>();
    keyList.add(eData);

    {
      Map<byte[], byte[]> result = transitEngine.reWrapString(key, keyList);
      LOG.debug("Received ReWraped Data: {}, eData is {}", result.toString(), eData);
      assertEquals(keyList.size(), result.size());

      rbytes = result.get(eData);
      assertNotEquals(0, rbytes.length);
      LOG.debug("Received ReWraped Data: {}", new String(rbytes, StandardCharsets.UTF_8));
    }

    // assert on original data and data from -
    // encrypt{d,e} -> rewrap{e,e`} -> decrypt{e`,d}
    // rbytes = "ENCRYPTED2_TestData".getBytes();
    byte[] dData2 = transitEngine.decryptString(key, rbytes);
    assertEquals(
        new String(data, StandardCharsets.UTF_8), new String(dData2, StandardCharsets.UTF_8));

    if (MOCK_RUN) System.out.println(mockingDetails(vAccessor).getInvocations());
  }
}
