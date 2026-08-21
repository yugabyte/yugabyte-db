/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
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
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.oracle.bmc.Region;
import com.oracle.bmc.auth.AuthenticationDetailsProvider;
import com.oracle.bmc.auth.SimpleAuthenticationDetailsProvider;
import com.oracle.bmc.keymanagement.KmsCryptoClient;
import com.oracle.bmc.keymanagement.KmsManagementClient;
import com.oracle.bmc.keymanagement.KmsVaultClient;
import com.oracle.bmc.keymanagement.model.Key;
import com.oracle.bmc.keymanagement.responses.GetKeyResponse;
import com.oracle.bmc.model.BmcException;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Universe;
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
public class OciEARServiceUtilTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(OciEARServiceUtil.class);

  private final ObjectMapper mapper = new ObjectMapper();
  private ObjectNode fakeAuthConfig = mapper.createObjectNode();
  public UUID configUUID = UUID.randomUUID();
  private String authConfigName = "fake-oci-kms-config";
  private String tenancyOcid = "ocid1.tenancy.oc1..fake";
  private String userOcid = "ocid1.user.oc1..fake";
  private String fingerprint = "fake-fingerprint";
  private String privateKey = "fake-pem-key";
  private String compartmentOcid = "ocid1.compartment.oc1..fake";
  private String vaultOcid = "ocid1.vault.oc1..fake";
  private String region = "us-phoenix-1";
  private String keyOcid = "ocid1.key.oc1..fake";
  private String keyName = "fake-key-name";
  private Customer customer;
  private Universe universe;

  @Spy public OciEARServiceUtil mockOciEARServiceUtil;
  @Mock public KmsVaultClient fakeclient;
  @Mock public AuthenticationDetailsProvider fakeAuthProvider;
  @Mock public KmsManagementClient fakeKmsManagementClient;
  @Mock public KmsCryptoClient fakecryptoClient;

  @Before
  public void setUp() {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());

    fakeAuthConfig = mapper.createObjectNode();
    fakeAuthConfig.put("name", authConfigName);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.ociTenancyId.fieldName, tenancyOcid);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.ociUserId.fieldName, userOcid);
    fakeAuthConfig.put(
        OciEARServiceUtil.OciKmsAuthConfigField.ociFingerprint.fieldName, fingerprint);
    fakeAuthConfig.put(
        OciEARServiceUtil.OciKmsAuthConfigField.ociPrivateKeyContent.fieldName, privateKey);
    fakeAuthConfig.put(
        OciEARServiceUtil.OciKmsAuthConfigField.ociCompartmentId.fieldName, compartmentOcid);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.ociVaultId.fieldName, vaultOcid);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.ociRegion.fieldName, region);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.ociKeyName.fieldName, keyName);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.ociKeyOcid.fieldName, keyOcid);

    doReturn(fakeclient).when(mockOciEARServiceUtil).getKmsVaultClient(configUUID, fakeAuthConfig);
    doReturn(fakeKmsManagementClient)
        .when(mockOciEARServiceUtil)
        .getKmsManagementClient(configUUID, fakeAuthConfig);
    doReturn(fakecryptoClient)
        .when(mockOciEARServiceUtil)
        .getKmsCryptoClient(configUUID, fakeAuthConfig);
  }

  @Test
  public void testGetKmsVaultClient() {
    KmsVaultClient client = mockOciEARServiceUtil.getKmsVaultClient(configUUID, fakeAuthConfig);
    assertNotNull(client);
    assertEquals(client, fakeclient);
  }

  @Test
  public void testGetKmsManagementClient() {
    KmsManagementClient kmsManagementClient =
        mockOciEARServiceUtil.getKmsManagementClient(configUUID, fakeAuthConfig);
    assertNotNull(kmsManagementClient);
    assertEquals(kmsManagementClient, fakeKmsManagementClient);
  }

  @Test
  public void testGetKmsCryptoClient() {
    KmsCryptoClient cryptoClient =
        mockOciEARServiceUtil.getKmsCryptoClient(configUUID, fakeAuthConfig);
    assertNotNull(cryptoClient);
    assertEquals(cryptoClient, fakecryptoClient);
  }

  @Test
  public void testGetKeyOcid() {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setConfigUUID(configUUID);
    kmsConfig.setCustomerUUID(customer.getUuid());
    kmsConfig.setKeyProvider(KeyProvider.OCI);
    kmsConfig.setAuthConfig(fakeAuthConfig);
    kmsConfig.setVersion(KmsConfig.SCHEMA_VERSION);
    kmsConfig.setName(authConfigName);
    kmsConfig.save();

    String result = mockOciEARServiceUtil.getKeyOcid(configUUID);
    assertEquals(keyOcid, result);
  }

  @Test
  public void testResolveKeyOcid_cachedOcid_returnsWithoutLookup() {
    // When an OCID is already cached, it is returned as-is.
    String result = mockOciEARServiceUtil.resolveKeyOcid(configUUID, fakeAuthConfig);
    assertEquals(keyOcid, result);
  }

  @Test
  public void testResolveKeyOcid_fromName_resolvesAndCaches() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.remove(OciEARServiceUtil.OciKmsAuthConfigField.ociKeyOcid.fieldName);

    doReturn(keyOcid).when(mockOciEARServiceUtil).getKeyOcidByName(any(), any(), eq(keyName));

    String result = mockOciEARServiceUtil.resolveKeyOcid(configUUID, form);
    assertEquals(keyOcid, result);
    assertEquals(
        keyOcid, form.path(OciEARServiceUtil.OciKmsAuthConfigField.ociKeyOcid.fieldName).asText());
  }

  @Test(expected = RuntimeException.class)
  public void testGetCredentials_missingField_throws() {
    ObjectNode bad = fakeAuthConfig.deepCopy();
    bad.put(OciEARServiceUtil.OciKmsAuthConfigField.ociPrivateKeyContent.fieldName, "");
    mockOciEARServiceUtil.getCredentials(bad);
  }

  @Test
  public void testValidateKMSProviderConfigFormData_missingCompartment_throws() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.put(OciEARServiceUtil.OciKmsAuthConfigField.ociCompartmentId.fieldName, "");
    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(form);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("OCI_COMPARTMENT_OCID"));
    }
  }

  @Test
  public void testValidateKMSProviderConfigFormData_missingVault_throws() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.put(OciEARServiceUtil.OciKmsAuthConfigField.ociVaultId.fieldName, "");
    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(form);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("OCI_VAULT_OCID"));
    }
  }

  @Test
  public void testValidateKMSProviderConfigFormData_missingRegion_throws() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.put(OciEARServiceUtil.OciKmsAuthConfigField.ociRegion.fieldName, "");
    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(form);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("OCI_REGION"));
    }
  }

  @Test
  public void testValidateKMSProviderConfigFormData_invalidRegionString_throws() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.put(OciEARServiceUtil.OciKmsAuthConfigField.ociRegion.fieldName, "not-a-real-region");
    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(form);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("not-a-real-region"));
      assertTrue(e.getMessage().contains("OCI_REGION"));
    }
  }

  @Test
  public void testValidateKMSProviderConfigFormData_invalidCredentials_throws() {
    // Stub the protected credential-validation helper to simulate an OCI 401 response.
    BmcException authFailure = mock(BmcException.class);
    when(authFailure.getStatusCode()).thenReturn(401);
    doThrow(authFailure)
        .when(mockOciEARServiceUtil)
        .validateCredentialsWithIdentityService(
            any(Region.class), any(SimpleAuthenticationDetailsProvider.class));

    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(fakeAuthConfig);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("TENANCY_OCID, USER_OCID, FINGERPRINT, and PRIVATE_KEY"));
    }
  }

  @Test
  public void testValidateKMSProviderConfigFormData_vaultNotFound_throws() {
    // Credentials pass; vault lookup returns 404.
    doNothing()
        .when(mockOciEARServiceUtil)
        .validateCredentialsWithIdentityService(
            any(Region.class), any(SimpleAuthenticationDetailsProvider.class));
    doReturn(fakeclient).when(mockOciEARServiceUtil).getKmsVaultClient(null, fakeAuthConfig);
    BmcException notFound = mock(BmcException.class);
    when(notFound.getStatusCode()).thenReturn(404);
    doThrow(notFound).when(mockOciEARServiceUtil).getVaultFromId(fakeclient, vaultOcid);

    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(fakeAuthConfig);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("OCI_VAULT_OCID"));
      assertTrue(e.getMessage().contains(vaultOcid));
      assertTrue(e.getMessage().contains("OCI_REGION"));
    }
  }

  // ---- checkKeyExists tests ----

  @Test
  public void testCheckKeyExists_blankKeyOcid_returnsFalse() {
    assertFalse(mockOciEARServiceUtil.checkKeyExists(fakeAuthConfig, ""));
    assertFalse(mockOciEARServiceUtil.checkKeyExists(fakeAuthConfig, null));
  }

  @Test
  public void testCheckKeyExists_keyNotFound_returnsFalse() {
    doReturn(fakeKmsManagementClient)
        .when(mockOciEARServiceUtil)
        .getKmsManagementClient(null, fakeAuthConfig);
    BmcException notFound = mock(BmcException.class);
    when(notFound.getStatusCode()).thenReturn(404);
    doThrow(notFound).when(fakeKmsManagementClient).getKey(any());

    assertFalse(mockOciEARServiceUtil.checkKeyExists(fakeAuthConfig, keyOcid));
  }

  @Test(expected = RuntimeException.class)
  public void testCheckKeyExists_insufficientPermissions_throws() {
    doReturn(fakeKmsManagementClient)
        .when(mockOciEARServiceUtil)
        .getKmsManagementClient(null, fakeAuthConfig);
    BmcException forbidden = mock(BmcException.class);
    when(forbidden.getStatusCode()).thenReturn(403);
    doThrow(forbidden).when(fakeKmsManagementClient).getKey(any());

    mockOciEARServiceUtil.checkKeyExists(fakeAuthConfig, keyOcid);
  }

  @Test
  public void testCheckKeyExists_keyFound_returnsTrue() {
    doReturn(fakeKmsManagementClient)
        .when(mockOciEARServiceUtil)
        .getKmsManagementClient(null, fakeAuthConfig);

    Key existingKey = mock(Key.class);
    GetKeyResponse response = mock(GetKeyResponse.class);
    when(response.getKey()).thenReturn(existingKey);
    doReturn(response).when(fakeKmsManagementClient).getKey(any());

    assertTrue(mockOciEARServiceUtil.checkKeyExists(fakeAuthConfig, keyOcid));
  }

  @Test
  public void testCheckKeyExists_403_errorMessageMentionsPermissions() {
    doReturn(fakeKmsManagementClient)
        .when(mockOciEARServiceUtil)
        .getKmsManagementClient(null, fakeAuthConfig);
    BmcException forbidden = mock(BmcException.class);
    when(forbidden.getStatusCode()).thenReturn(403);
    doThrow(forbidden).when(fakeKmsManagementClient).getKey(any());

    try {
      mockOciEARServiceUtil.checkKeyExists(fakeAuthConfig, keyOcid);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("Insufficient permissions"));
      assertTrue(e.getMessage().contains(keyOcid));
    }
  }
}
