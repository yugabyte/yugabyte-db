/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.cloud.kms.v1.CryptoKey;
import com.google.cloud.kms.v1.KeyManagementServiceClient;
import com.google.cloud.kms.v1.KeyRing;
import com.google.cloud.kms.v1.LocationName;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class GcpEARServiceUtilTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(GcpEARServiceUtil.class);

  // Create fake auth config details
  public ObjectMapper mapper = new ObjectMapper();
  public ObjectNode fakeAuthConfig = mapper.createObjectNode();
  public UUID configUUID = UUID.randomUUID();
  public String authConfigName = "fake-gcp-kms-config";
  public String authConfigLocationId = "global";
  public String authConfigProtectionLevel = "HSM";
  public String authConfigProjectId = "yugabyte";
  // Create fake key ring and crypto key resource names
  public String fakeKeyRingRN = "projects/yugabyte/locations/global/keyRings/yb-kr";
  public String fakeKeyRingId = "yb-kr";
  public String fakeCryptoKeyRN =
      "projects/yugabyte/locations/global/keyRings/yb-kr/cryptoKeys/yb-ck";
  public String fakeCryptoKeyId = "yb-ck";
  public String fakeKeyVersionRN =
      "projects/yugabyte/locations/global/keyRings/yb-kr/cryptoKeys/yb-ck/cryptoKeyVersions/2";
  public String fakeKeyVersionId = "2";
  public Universe universe;
  public Customer customer;

  @Spy public GcpEARServiceUtil mockGcpEARServiceUtil;
  @Mock public KeyManagementServiceClient mockClient;
  @Mock public KeyRing mockKeyRing;
  @Mock public CryptoKey mockCryptoKey;

  @Before
  public void setUp() throws IOException {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());

    // Populate the fake auth config
    fakeAuthConfig.put("name", authConfigName);
    fakeAuthConfig.put("LOCATION_ID", authConfigLocationId);
    fakeAuthConfig.put("PROTECTION_LEVEL", authConfigProtectionLevel);
    fakeAuthConfig.put("GCP_KMS_ENDPOINT", "fake-kms-endpoint");
    fakeAuthConfig.put("KEY_RING_ID", fakeKeyRingId);
    fakeAuthConfig.put("CRYPTO_KEY_ID", fakeCryptoKeyId);

    ObjectNode fakeGcpConfig = fakeAuthConfig.putObject("GCP_CONFIG");
    fakeGcpConfig.put("type", "service_account");
    fakeGcpConfig.put("project_id", authConfigProjectId);

    // Spy the util class
    // Mock all methods that are called on the GCP KMS client, return fake data
    doNothing().when(mockClient).close();
    doReturn(mockKeyRing).when(mockClient).getKeyRing(fakeKeyRingRN);
    doReturn(mockCryptoKey).when(mockClient).getCryptoKey(fakeCryptoKeyRN);
    doReturn(mockKeyRing)
        .when(mockClient)
        .createKeyRing(any(LocationName.class), anyString(), any());

    doReturn(mockClient).when(mockGcpEARServiceUtil).getKMSClient(fakeAuthConfig);
    doReturn(fakeCryptoKeyRN).when(mockGcpEARServiceUtil).getCryptoKeyRN(any());
  }

  @Test
  public void testGetConfigProjectId() {
    String projectId = mockGcpEARServiceUtil.getConfigProjectId(fakeAuthConfig);
    assertEquals(projectId, authConfigProjectId);
  }

  @Test
  public void testGetConfigProjectIdAuthConfig() {
    String projectId = mockGcpEARServiceUtil.getConfigProjectId(fakeAuthConfig);
    assertEquals(projectId, authConfigProjectId);
  }

  @Test
  public void testGetConfigLocationId() {
    String locationId = mockGcpEARServiceUtil.getConfigLocationId(fakeAuthConfig);
    assertEquals(locationId, authConfigLocationId);
  }

  @Test
  public void testGetConfigLocationIdAuthConfig() {
    String locationId = mockGcpEARServiceUtil.getConfigLocationId(fakeAuthConfig);
    assertEquals(locationId, authConfigLocationId);
  }

  @Test
  public void testGetKeyRing() {
    KeyRing keyRing = mockGcpEARServiceUtil.getKeyRing(fakeAuthConfig);
    assertEquals(mockKeyRing, keyRing);
  }

  @Test
  public void testGetKeyRingRN() {
    String keyRingRN = mockGcpEARServiceUtil.getKeyRingRN(fakeAuthConfig);
    assertEquals(keyRingRN, fakeKeyRingRN);
  }

  @Test
  public void testGetKeyRingIdFromConfig() {
    String keyRingId = mockGcpEARServiceUtil.getConfigKeyRingId(fakeAuthConfig);
    assertEquals(keyRingId, fakeKeyRingId);
  }

  @Test
  public void testGetCryptoKey() {
    CryptoKey cryptoKey = mockGcpEARServiceUtil.getCryptoKey(fakeAuthConfig);
    assertEquals(cryptoKey, mockCryptoKey);
  }

  @Test
  public void testGetKeyRingIdFromRN() {
    String keyRingId = mockGcpEARServiceUtil.getKeyRingIdFromRN(fakeKeyRingRN);
    assertEquals(keyRingId, fakeKeyRingId);
  }

  @Test
  public void testGetCryptoKeyIdFromRN() {
    String cryptoKeyId = mockGcpEARServiceUtil.getCryptoKeyIdFromRN(fakeCryptoKeyRN);
    assertEquals(cryptoKeyId, fakeCryptoKeyId);
  }

  @Test
  public void testGetKeyVersionIdFromRN() {
    String keyVersionId = mockGcpEARServiceUtil.getKeyVersionIdFromRN(fakeKeyVersionRN);
    assertEquals(keyVersionId, fakeKeyVersionId);
  }

  @Test
  public void testCreateKeyRing() throws IOException {
    KeyRing keyRing = mockGcpEARServiceUtil.createKeyRing(fakeAuthConfig);
    assertEquals(keyRing, mockKeyRing);
  }
}
