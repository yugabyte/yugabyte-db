/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 * POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.services;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.EncryptionKey;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthConfigField;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import org.apache.commons.lang3.RandomUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class OciEARServiceTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(OciEARService.class);

  public ObjectMapper mapper = new ObjectMapper();
  public ObjectNode fakeAuthConfig = mapper.createObjectNode();
  public UUID configUUID = UUID.randomUUID();
  public String authConfigName = "fake-oci-kms-config";
  public String keyOcid = "ocid1.key.oc1..fake";
  public String keyName = "fake-key-name";
  public int numBytes = 32;
  public byte[] randomBytes = RandomUtils.nextBytes(numBytes);
  public Universe universe;
  public Customer customer;

  @Spy public OciEARServiceUtil mockOciEARServiceUtil;
  @Spy public OciEARService mockOciEARService = new OciEARService(null);

  @Before
  public void setUp() throws Exception {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());

    fakeAuthConfig.put("name", authConfigName);
    fakeAuthConfig.put(OciKmsAuthConfigField.TENANCY_OCID.fieldName, "ocid1.tenancy.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.USER_OCID.fieldName, "ocid1.user.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.FINGERPRINT.fieldName, "fake-fingerprint");
    fakeAuthConfig.put(OciKmsAuthConfigField.PRIVATE_KEY.fieldName, "fake-pem-key");
    fakeAuthConfig.put(
        OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName, "ocid1.compartment.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.OCI_VAULT_OCID.fieldName, "ocid1.vault.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.OCI_REGION.fieldName, "us-phoenix-1");
    fakeAuthConfig.put(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName, keyOcid);

    doReturn(mockOciEARServiceUtil).when(mockOciEARService).getOciEarServiceUtil();

    doReturn(randomBytes)
        .when(mockOciEARServiceUtil)
        .generateDataEncryptionKey(eq(configUUID), any(), eq(keyOcid));
    doReturn(randomBytes)
        .when(mockOciEARServiceUtil)
        .encryptUniverseKey(eq(configUUID), any(byte[].class), any());
    doReturn(randomBytes)
        .when(mockOciEARServiceUtil)
        .decryptUniverseKey(eq(configUUID), eq(randomBytes), any());
  }

  @Test
  public void testCreateAuthConfigWithService_withKeyOcid() throws Exception {
    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    fakeAuthConfigCopy.put(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName, keyOcid);

    ObjectNode createdAuthConfig =
        mockOciEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertNotNull(createdAuthConfig);
    assertEquals(
        keyOcid, createdAuthConfig.path(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName).asText());
  }

  @Test
  public void testCreateAuthConfigWithService_withKeyname() throws Exception {
    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    fakeAuthConfigCopy.remove(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName);
    fakeAuthConfigCopy.put(OciKmsAuthConfigField.OCI_KEY_NAME.fieldName, keyName);

    doReturn(keyOcid)
        .when(mockOciEARServiceUtil)
        .getKeyOcidByName(eq(configUUID), any(), eq(keyName));

    ObjectNode createdAuthConfig =
        mockOciEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertNotNull(createdAuthConfig);
    assertEquals(
        keyOcid, createdAuthConfig.path(OciKmsAuthConfigField.OCI_KEY_OCID.fieldName).asText());
    assertEquals(
        keyName, createdAuthConfig.path(OciKmsAuthConfigField.OCI_KEY_NAME.fieldName).asText());
  }

  @Test
  public void testCreateKeyWithService() throws Exception {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setConfigUUID(configUUID);
    kmsConfig.setCustomerUUID(customer.getUuid());
    kmsConfig.setKeyProvider(KeyProvider.OCI);
    kmsConfig.setAuthConfig(fakeAuthConfig);
    kmsConfig.setVersion(KmsConfig.SCHEMA_VERSION);
    kmsConfig.setName(authConfigName);
    kmsConfig.save();

    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    encryptionAtRestConfig.type = KeyType.DATA_KEY;

    doReturn(keyOcid).when(mockOciEARServiceUtil).getKeyOcid(configUUID);

    EncryptionKey keyRef =
        mockOciEARService.createKeyWithService(
            universe.getUniverseUUID(), configUUID, encryptionAtRestConfig);
    assertNotNull(keyRef);
    assertNotNull(keyRef.getKeyBytes());
    assertEquals(randomBytes, keyRef.getKeyBytes());
    verify(mockOciEARServiceUtil, times(1))
        .generateDataEncryptionKey(eq(configUUID), any(), eq(keyOcid));
  }

  @Test
  public void testRotateKeyWithService() throws Exception {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setConfigUUID(configUUID);
    kmsConfig.setCustomerUUID(customer.getUuid());
    kmsConfig.setKeyProvider(KeyProvider.OCI);
    kmsConfig.setAuthConfig(fakeAuthConfig);
    kmsConfig.setVersion(KmsConfig.SCHEMA_VERSION);
    kmsConfig.setName(authConfigName);
    kmsConfig.save();

    doReturn(keyOcid).when(mockOciEARServiceUtil).getKeyOcid(configUUID);

    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    EncryptionKey keyRef =
        mockOciEARService.rotateKeyWithService(
            universe.getUniverseUUID(), configUUID, encryptionAtRestConfig);
    assertNotNull(keyRef);
    assertEquals(randomBytes, keyRef.getKeyBytes());
    verify(mockOciEARServiceUtil, times(1))
        .generateDataEncryptionKey(eq(configUUID), any(), eq(keyOcid));
  }

  @Test
  public void testRetrieveKeyWithService() throws Exception {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setConfigUUID(configUUID);
    kmsConfig.setCustomerUUID(customer.getUuid());
    kmsConfig.setKeyProvider(KeyProvider.OCI);
    kmsConfig.setAuthConfig(fakeAuthConfig);
    kmsConfig.setVersion(KmsConfig.SCHEMA_VERSION);
    kmsConfig.setName(authConfigName);
    kmsConfig.save();

    byte[] keyRef = mockOciEARService.retrieveKeyWithService(configUUID, randomBytes, null);
    assertEquals(randomBytes, keyRef);
    verify(mockOciEARServiceUtil, times(1))
        .decryptUniverseKey(eq(configUUID), eq(randomBytes), any());
  }

  @Test
  public void testValidateRetrieveKeyWithService() throws Exception {
    byte[] keyRef =
        mockOciEARService.validateRetrieveKeyWithService(
            configUUID, randomBytes, null, fakeAuthConfig);
    assertEquals(randomBytes, keyRef);
    verify(mockOciEARServiceUtil, times(1))
        .decryptUniverseKey(eq(configUUID), eq(randomBytes), any());
  }

  @Test
  public void testEncryptKeyWithService() throws Exception {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setConfigUUID(configUUID);
    kmsConfig.setCustomerUUID(customer.getUuid());
    kmsConfig.setKeyProvider(KeyProvider.OCI);
    kmsConfig.setAuthConfig(fakeAuthConfig);
    kmsConfig.setVersion(KmsConfig.SCHEMA_VERSION);
    kmsConfig.setName(authConfigName);
    kmsConfig.save();

    EncryptionKey encKey = mockOciEARService.encryptKeyWithService(configUUID, randomBytes);
    assertNotNull(encKey);
    assertNotNull(encKey.getKeyBytes());
    assertEquals(randomBytes, encKey.getKeyBytes());
    verify(mockOciEARServiceUtil, times(1))
        .encryptUniverseKey(eq(configUUID), eq(randomBytes), any());
  }
}
