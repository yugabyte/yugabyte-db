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
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.oracle.bmc.keymanagement.model.Key;
import com.oracle.bmc.keymanagement.responses.CreateKeyResponse;
import com.oracle.bmc.model.BmcException;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YWErrorHandler;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.EncryptionKey;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.InvalidOciKmsConfigException;
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
  public String otherKeyOcid = "ocid1.key.oc1..example-other";
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
    fakeAuthConfig.put(OciKmsAuthConfigField.ociTenancyId.fieldName, "ocid1.tenancy.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.ociUserId.fieldName, "ocid1.user.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.ociFingerprint.fieldName, "fake-fingerprint");
    fakeAuthConfig.put(OciKmsAuthConfigField.ociPrivateKeyContent.fieldName, "fake-pem-key");
    fakeAuthConfig.put(
        OciKmsAuthConfigField.ociCompartmentId.fieldName, "ocid1.compartment.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.ociVaultId.fieldName, "ocid1.vault.oc1..fake");
    fakeAuthConfig.put(OciKmsAuthConfigField.ociRegion.fieldName, "us-phoenix-1");
    fakeAuthConfig.put(OciKmsAuthConfigField.ociKeyOcid.fieldName, keyOcid);

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
  public void testCreateAuthConfigWithService_keyExists_resolvesOcid() throws Exception {
    // OCI_KEY_NAME is the only user-facing input; the OCID is derived and cached.
    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    fakeAuthConfigCopy.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
    fakeAuthConfigCopy.put(OciKmsAuthConfigField.ociKeyName.fieldName, keyName);

    doReturn(keyOcid)
        .when(mockOciEARServiceUtil)
        .getKeyOcidByName(eq(configUUID), any(), eq(keyName));

    ObjectNode createdAuthConfig =
        mockOciEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertNotNull(createdAuthConfig);
    assertEquals(
        keyOcid, createdAuthConfig.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
    assertEquals(
        keyName, createdAuthConfig.path(OciKmsAuthConfigField.ociKeyName.fieldName).asText());
  }

  @Test
  public void testCreateAuthConfigWithService_keyDoesNotExist_createsAndCachesOcid()
      throws Exception {
    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    fakeAuthConfigCopy.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
    fakeAuthConfigCopy.put(OciKmsAuthConfigField.ociKeyName.fieldName, keyName);

    doReturn(null).when(mockOciEARServiceUtil).getKeyOcidByName(eq(configUUID), any(), eq(keyName));

    Key createdKey = mock(Key.class);
    when(createdKey.getId()).thenReturn(keyOcid);
    CreateKeyResponse createKeyResponse = mock(CreateKeyResponse.class);
    when(createKeyResponse.getKey()).thenReturn(createdKey);
    doReturn(createKeyResponse)
        .when(mockOciEARServiceUtil)
        .createKey(eq(configUUID), any(), eq(keyName));

    ObjectNode createdAuthConfig =
        mockOciEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertNotNull(createdAuthConfig);
    assertEquals(
        keyOcid, createdAuthConfig.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
    verify(mockOciEARServiceUtil, times(1)).createKey(eq(configUUID), any(), eq(keyName));
  }

  @Test
  public void testCreateAuthConfigWithService_userSuppliedOcid_isIgnored() throws Exception {
    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    fakeAuthConfigCopy.put(OciKmsAuthConfigField.ociKeyOcid.fieldName, otherKeyOcid);
    fakeAuthConfigCopy.put(OciKmsAuthConfigField.ociKeyName.fieldName, keyName);

    doReturn(keyOcid)
        .when(mockOciEARServiceUtil)
        .getKeyOcidByName(eq(configUUID), any(), eq(keyName));

    ObjectNode createdAuthConfig =
        mockOciEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertNotNull(createdAuthConfig);
    assertEquals(
        keyOcid, createdAuthConfig.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
  }

  @Test
  public void testRefreshKmsWithService_storedOcid_usesStoredKeyWithoutNameLookup()
      throws Exception {
    // The key was renamed in OCI, so a lookup by the stored name finds nothing.
    ObjectNode authConfig = fakeAuthConfig.deepCopy();
    authConfig.put(OciKmsAuthConfigField.ociKeyName.fieldName, keyName);
    stubRenamedKey();
    doNothing().when(mockOciEARServiceUtil).validateKMSProviderConfigFormData(any(), eq(false));
    doNothing().when(mockOciEARServiceUtil).testWrapAndUnwrapKey(eq(configUUID), any());

    mockOciEARService.refreshKmsWithService(configUUID, authConfig);

    verify(mockOciEARServiceUtil, never()).getKeyOcidByName(any(), any(), any());
    verify(mockOciEARServiceUtil, never()).createKey(any(), any(), any());
    assertEquals(keyOcid, authConfig.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
  }

  @Test
  public void testRefreshKmsWithService_legacyConfigWithoutOcid_looksUpByNameWithoutCreating()
      throws Exception {
    ObjectNode authConfig = fakeAuthConfig.deepCopy();
    authConfig.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
    authConfig.put(OciKmsAuthConfigField.ociKeyName.fieldName, keyName);
    doReturn(keyOcid)
        .when(mockOciEARServiceUtil)
        .getKeyOcidByName(eq(configUUID), any(), eq(keyName));
    doNothing().when(mockOciEARServiceUtil).validateKMSProviderConfigFormData(any(), eq(false));
    doNothing().when(mockOciEARServiceUtil).testWrapAndUnwrapKey(eq(configUUID), any());

    mockOciEARService.refreshKmsWithService(configUUID, authConfig);

    verify(mockOciEARServiceUtil, never()).createKey(any(), any(), any());
    assertEquals(keyOcid, authConfig.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
  }

  @Test
  public void testRefreshKmsWithService_storedKeyNotEnabled_failsWithBadRequest() throws Exception {
    ObjectNode authConfig = withKeyName(fakeAuthConfig);
    stubRenamedKey();
    doThrow(new InvalidOciKmsConfigException(notEnabledMessage()))
        .when(mockOciEARServiceUtil)
        .validateKMSProviderConfigFormData(any(), eq(false));

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> mockOciEARService.refreshKmsWithService(configUUID, authConfig));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertTrue(e.getMessage().contains(keyOcid));
    verify(mockOciEARServiceUtil, never()).testWrapAndUnwrapKey(any(), any());
    verify(mockOciEARServiceUtil, never()).createKey(any(), any(), any());
  }

  @Test
  public void testRefreshKms_invalidKey_reachesClientAsBadRequest() throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    doThrow(new InvalidOciKmsConfigException(notEnabledMessage()))
        .when(mockOciEARServiceUtil)
        .validateKMSProviderConfigFormData(any(), eq(false));

    // refreshKms wraps the failure; the HTTP error handler finds the 400 in the cause chain.
    RuntimeException e =
        assertThrows(RuntimeException.class, () -> mockOciEARService.refreshKms(configUUID));
    PlatformServiceException response =
        YWErrorHandler.toPlatformServiceException(e).orElseThrow(AssertionError::new);
    assertEquals(BAD_REQUEST, response.getHttpStatus());
    assertEquals(notEnabledMessage(), response.getMessage());
  }

  @Test
  public void testRefreshKmsWithService_unexpectedErrorInValidation_isNotTurnedIntoBadRequest()
      throws Exception {
    ObjectNode authConfig = withKeyName(fakeAuthConfig);
    NullPointerException unexpected = new NullPointerException();
    doThrow(unexpected)
        .when(mockOciEARServiceUtil)
        .validateKMSProviderConfigFormData(any(), eq(false));

    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () -> mockOciEARService.refreshKmsWithService(configUUID, authConfig));
    assertSame(unexpected, e);
  }

  @Test
  public void testRefreshKmsWithService_ociErrorInValidation_isNotTurnedIntoBadRequest()
      throws Exception {
    ObjectNode authConfig = withKeyName(fakeAuthConfig);
    BmcException throttled = mock(BmcException.class);
    lenient().when(throttled.getStatusCode()).thenReturn(429);
    RuntimeException ociError = new RuntimeException("OCI credential validation failed", throttled);
    doThrow(ociError)
        .when(mockOciEARServiceUtil)
        .validateKMSProviderConfigFormData(any(), eq(false));

    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () -> mockOciEARService.refreshKmsWithService(configUUID, authConfig));
    assertSame(ociError, e);
  }

  @Test
  public void testUpdateAuthConfig_keepsStoredKeyOcid() throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    stubRenamedKey();
    doReturn(true).when(mockOciEARServiceUtil).validateKeySettings(any(), eq(keyOcid));

    // The edit form carries the stored OCID, copied in by the controller.
    ObjectNode editForm = withKeyName(fakeAuthConfig);
    editForm.put(OciKmsAuthConfigField.ociFingerprint.fieldName, "new-fake-fingerprint");

    KmsConfig updated = mockOciEARService.updateAuthConfig(configUUID, editForm);

    assertNotNull(updated);
    ObjectNode stored = mockOciEARService.getAuthConfig(configUUID);
    assertEquals(keyOcid, stored.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
    assertEquals(
        "new-fake-fingerprint",
        stored.path(OciKmsAuthConfigField.ociFingerprint.fieldName).asText());
    verify(mockOciEARServiceUtil, never()).getKeyOcidByName(any(), any(), any());
    verify(mockOciEARServiceUtil, never()).createKey(any(), any(), any());
  }

  @Test
  public void testUpdateAuthConfig_formWithoutOrWithOtherOcid_keepsStoredKeyOcid()
      throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    stubRenamedKey();
    doReturn(true).when(mockOciEARServiceUtil).validateKeySettings(any(), eq(keyOcid));

    ObjectNode formWithoutOcid = withKeyName(fakeAuthConfig);
    formWithoutOcid.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
    assertNotNull(mockOciEARService.updateAuthConfig(configUUID, formWithoutOcid));
    assertEquals(
        keyOcid,
        mockOciEARService
            .getAuthConfig(configUUID)
            .path(OciKmsAuthConfigField.ociKeyOcid.fieldName)
            .asText());

    ObjectNode formWithOtherOcid = withKeyName(fakeAuthConfig);
    formWithOtherOcid.put(OciKmsAuthConfigField.ociKeyOcid.fieldName, otherKeyOcid);
    assertNotNull(mockOciEARService.updateAuthConfig(configUUID, formWithOtherOcid));
    assertEquals(
        keyOcid,
        mockOciEARService
            .getAuthConfig(configUUID)
            .path(OciKmsAuthConfigField.ociKeyOcid.fieldName)
            .asText());
    verify(mockOciEARServiceUtil, never()).createKey(any(), any(), any());
  }

  @Test
  public void testUpdateAuthConfig_storedKeyNotEnabled_failsAndKeepsStoredConfig()
      throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    stubRenamedKey();
    doReturn(false).when(mockOciEARServiceUtil).validateKeySettings(any(), eq(keyOcid));

    ObjectNode editForm = withKeyName(fakeAuthConfig);
    editForm.put(OciKmsAuthConfigField.ociFingerprint.fieldName, "new-fake-fingerprint");

    RuntimeException e =
        assertThrows(
            RuntimeException.class, () -> mockOciEARService.updateAuthConfig(configUUID, editForm));
    assertTrue(e.getMessage().contains(keyOcid));
    ObjectNode stored = mockOciEARService.getAuthConfig(configUUID);
    assertEquals(keyOcid, stored.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
    assertEquals(
        "fake-fingerprint", stored.path(OciKmsAuthConfigField.ociFingerprint.fieldName).asText());
    verify(mockOciEARServiceUtil, never()).createKey(any(), any(), any());
  }

  @Test
  public void testUpdateAuthConfig_storedKeyDeleted_reportsMissingKey() throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    stubRenamedKey();
    doReturn(false).when(mockOciEARServiceUtil).checkKeyExists(any(), eq(keyOcid));

    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () -> mockOciEARService.updateAuthConfig(configUUID, withKeyName(fakeAuthConfig)));
    assertTrue(e.getMessage().contains("OCI key '" + keyOcid + "' does not exist."));
    verify(mockOciEARServiceUtil, never()).validateKeySettings(any(), any());
  }

  @Test
  public void testUpdateAuthConfig_keyAccessDenied_keepsOciCause() throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    stubRenamedKey();
    BmcException forbidden = mock(BmcException.class);
    lenient().when(forbidden.getStatusCode()).thenReturn(403);
    RuntimeException accessError =
        new RuntimeException("Insufficient permissions to access OCI key.", forbidden);
    doThrow(accessError).when(mockOciEARServiceUtil).checkKeyExists(any(), eq(keyOcid));

    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () -> mockOciEARService.updateAuthConfig(configUUID, withKeyName(fakeAuthConfig)));
    assertSame(accessError, e.getCause());
    assertFalse(e.getMessage().contains("Enabled state"));
  }

  @Test
  public void testValidateRetrieveKeyWithService_decryptFails_throwsWithReason() throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    RuntimeException decryptError = new RuntimeException("key is disabled");
    doThrow(decryptError)
        .when(mockOciEARServiceUtil)
        .decryptUniverseKey(eq(configUUID), eq(randomBytes), any());

    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () ->
                mockOciEARService.validateRetrieveKeyWithService(
                    configUUID, randomBytes, null, withKeyName(fakeAuthConfig)));
    assertTrue(e.getMessage().contains("key is disabled"));
    assertSame(decryptError, e.getCause());
  }

  @Test
  public void testUpdateAuthConfig_legacyConfigNameMatchesNoKey_failsWithReason() throws Exception {
    ObjectNode legacyConfig = withKeyName(fakeAuthConfig);
    legacyConfig.remove(OciKmsAuthConfigField.ociKeyOcid.fieldName);
    saveKmsConfig(legacyConfig);
    stubRenamedKey();

    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () -> mockOciEARService.updateAuthConfig(configUUID, legacyConfig.deepCopy()));
    assertTrue(e.getMessage().contains(keyName));
    assertTrue(
        mockOciEARService
            .getAuthConfig(configUUID)
            .path(OciKmsAuthConfigField.ociKeyOcid.fieldName)
            .isMissingNode());
    verify(mockOciEARServiceUtil, never()).createKey(any(), any(), any());
  }

  @Test
  public void testValidateRetrieveKeyWithService_formWithOtherOcid_decryptsWithStoredKey()
      throws Exception {
    saveKmsConfig(withKeyName(fakeAuthConfig));
    // An operator edit form whose CR names a different key OCID.
    ObjectNode editForm = withKeyName(fakeAuthConfig);
    editForm.put(OciKmsAuthConfigField.ociKeyOcid.fieldName, otherKeyOcid);

    byte[] keyRef =
        mockOciEARService.validateRetrieveKeyWithService(configUUID, randomBytes, null, editForm);

    assertEquals(randomBytes, keyRef);
    verify(mockOciEARServiceUtil, times(1))
        .decryptUniverseKey(
            eq(configUUID),
            eq(randomBytes),
            argThat(
                c -> keyOcid.equals(c.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText())));
    assertEquals(otherKeyOcid, editForm.path(OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
  }

  private String notEnabledMessage() {
    return "OCI key '"
        + keyOcid
        + "' has invalid settings: it must be in Enabled state and use AES algorithm.";
  }

  private ObjectNode withKeyName(ObjectNode authConfig) {
    ObjectNode copy = authConfig.deepCopy();
    copy.put(OciKmsAuthConfigField.ociKeyName.fieldName, keyName);
    return copy;
  }

  private void saveKmsConfig(ObjectNode authConfig) {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setConfigUUID(configUUID);
    kmsConfig.setCustomerUUID(customer.getUuid());
    kmsConfig.setKeyProvider(KeyProvider.OCI);
    kmsConfig.setAuthConfig(authConfig);
    kmsConfig.setVersion(KmsConfig.SCHEMA_VERSION);
    kmsConfig.setName(authConfigName);
    kmsConfig.save();
  }

  // The stored key is Enabled under a new name. A name lookup misses, and the key it would create
  // is not yet Enabled.
  private void stubRenamedKey() {
    lenient().doReturn(null).when(mockOciEARServiceUtil).getKeyOcidByName(any(), any(), any());
    Key createdKey = mock(Key.class);
    lenient().when(createdKey.getId()).thenReturn(otherKeyOcid);
    CreateKeyResponse createKeyResponse = mock(CreateKeyResponse.class);
    lenient().when(createKeyResponse.getKey()).thenReturn(createdKey);
    lenient()
        .doReturn(createKeyResponse)
        .when(mockOciEARServiceUtil)
        .createKey(any(), any(), any());
    lenient().doReturn(true).when(mockOciEARServiceUtil).checkKeyExists(any(), eq(keyOcid));
    lenient().doReturn(true).when(mockOciEARServiceUtil).checkKeyExists(any(), eq(otherKeyOcid));
    lenient().doReturn(true).when(mockOciEARServiceUtil).validateKeySettings(any(), eq(keyOcid));
    lenient()
        .doReturn(false)
        .when(mockOciEARServiceUtil)
        .validateKeySettings(any(), eq(otherKeyOcid));
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
    saveKmsConfig(fakeAuthConfig);
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
