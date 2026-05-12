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
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.oracle.bmc.auth.AuthenticationDetailsProvider;
import com.oracle.bmc.keymanagement.KmsCryptoClient;
import com.oracle.bmc.keymanagement.KmsManagementClient;
import com.oracle.bmc.keymanagement.KmsVaultClient;
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
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.TENANCY_OCID.fieldName, tenancyOcid);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.USER_OCID.fieldName, userOcid);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.FINGERPRINT.fieldName, fingerprint);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.PRIVATE_KEY.fieldName, privateKey);
    fakeAuthConfig.put(
        OciEARServiceUtil.OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName, compartmentOcid);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.OCI_VAULT_OCID.fieldName, vaultOcid);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.OCI_REGION.fieldName, region);
    fakeAuthConfig.put(OciEARServiceUtil.OciKmsAuthConfigField.OCI_KEY_OCID.fieldName, keyOcid);

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

  @Test(expected = RuntimeException.class)
  public void testGetCredentials_missingField_throws() {
    ObjectNode bad = fakeAuthConfig.deepCopy();
    bad.put(OciEARServiceUtil.OciKmsAuthConfigField.PRIVATE_KEY.fieldName, "");
    mockOciEARServiceUtil.getCredentials(bad);
  }

  @Test
  public void testValidateKMSProviderConfigFormData_missingCompartment_throws() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.put(OciEARServiceUtil.OciKmsAuthConfigField.OCI_COMPARTMENT_OCID.fieldName, "");
    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(form);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("OCI_COMPARTMENT_OCID"));
    }
  }

  @Test
  public void testValidateKMSProviderConfigFormData_missingVault_throws() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.put(OciEARServiceUtil.OciKmsAuthConfigField.OCI_VAULT_OCID.fieldName, "");
    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(form);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("OCI_VAULT_OCID"));
    }
  }

  @Test
  public void testValidateKMSProviderConfigFormData_missingRegion_throws() {
    ObjectNode form = fakeAuthConfig.deepCopy();
    form.put(OciEARServiceUtil.OciKmsAuthConfigField.OCI_REGION.fieldName, "");
    try {
      mockOciEARServiceUtil.validateKMSProviderConfigFormData(form);
    } catch (RuntimeException e) {
      assertTrue(e.getMessage().contains("OCI_REGION"));
    }
  }
}
