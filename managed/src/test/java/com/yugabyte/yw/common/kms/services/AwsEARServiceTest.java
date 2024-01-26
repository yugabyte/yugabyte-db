/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.services;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.amazonaws.services.kms.AWSKMS;
import com.amazonaws.services.kms.model.AliasListEntry;
import com.amazonaws.services.kms.model.CreateAliasRequest;
import com.amazonaws.services.kms.model.CreateKeyRequest;
import com.amazonaws.services.kms.model.CreateKeyResult;
import com.amazonaws.services.kms.model.DecryptRequest;
import com.amazonaws.services.kms.model.DecryptResult;
import com.amazonaws.services.kms.model.GenerateDataKeyWithoutPlaintextRequest;
import com.amazonaws.services.kms.model.GenerateDataKeyWithoutPlaintextResult;
import com.amazonaws.services.kms.model.KeyMetadata;
import com.amazonaws.services.kms.model.ListAliasesRequest;
import com.amazonaws.services.kms.model.ListAliasesResult;
import com.amazonaws.services.kms.model.UpdateAliasRequest;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AwsEARServiceTest extends FakeDBApplication {
  @Mock RuntimeConfGetter mockConfGetter;

  ApiHelper mockApiHelper;
  AwsEARService awsEARService;

  KeyProvider testKeyProvider = KeyProvider.AWS;

  String testCmkId = "fake-cmk-id";

  AWSKMS mockClient;
  ListAliasesResult mockAliasList;
  List<AliasListEntry> mockAliases;
  AliasListEntry mockAlias;
  CreateKeyResult mockCreateKeyResult;
  KeyMetadata mockKeyMetadata;
  GenerateDataKeyWithoutPlaintextResult mockDataKeyResult;
  DecryptResult mockDecryptResult;

  UUID testUniUUID = UUID.randomUUID();
  UUID testCustomerUUID = UUID.randomUUID();
  EncryptionAtRestConfig config;

  byte[] mockEncryptionKey = new String("tcIQ6E6HJu4m3C4NbVf/1yNe/6jYi/0LAYDsIouwcnU=").getBytes();

  ByteBuffer decryptedKeyBuffer = ByteBuffer.wrap(mockEncryptionKey);

  @Before
  public void setUp() {
    decryptedKeyBuffer.rewind();
    mockClient = mock(AWSKMS.class);
    mockAliasList = mock(ListAliasesResult.class);
    mockAlias = mock(AliasListEntry.class);
    mockAliases = new ArrayList<AliasListEntry>();
    mockCreateKeyResult = mock(CreateKeyResult.class);
    mockKeyMetadata = mock(KeyMetadata.class);
    mockDataKeyResult = mock(GenerateDataKeyWithoutPlaintextResult.class);
    mockDecryptResult = mock(DecryptResult.class);
    lenient()
        .when(mockAlias.getAliasName())
        .thenReturn(String.format("alias/%s", testUniUUID.toString()));
    lenient().when(mockAliasList.getAliases()).thenReturn(mockAliases);
    lenient().when(mockClient.listAliases(any(ListAliasesRequest.class))).thenReturn(mockAliasList);
    lenient()
        .when(mockClient.createKey(any(CreateKeyRequest.class)))
        .thenReturn(mockCreateKeyResult);
    lenient().when(mockCreateKeyResult.getKeyMetadata()).thenReturn(mockKeyMetadata);
    lenient().when(mockKeyMetadata.getKeyId()).thenReturn(testCmkId);
    lenient()
        .when(
            mockClient.generateDataKeyWithoutPlaintext(
                any(GenerateDataKeyWithoutPlaintextRequest.class)))
        .thenReturn(mockDataKeyResult);
    lenient()
        .when(mockDataKeyResult.getCiphertextBlob())
        .thenReturn(ByteBuffer.wrap(new String("some_universe_key_value_encrypted").getBytes()));
    lenient().when(mockClient.decrypt(any(DecryptRequest.class))).thenReturn(mockDecryptResult);
    lenient().when(mockDecryptResult.getPlaintext()).thenReturn(decryptedKeyBuffer);
    lenient()
        .when(
            mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(false);
    awsEARService = new AwsEARService(mockConfGetter);
    config = new EncryptionAtRestConfig();
    // TODO: (Daniel) - Create KMS Config and link to here
    config.kmsConfigUUID = null;
  }

  @Test
  @Ignore
  public void testCreateAndRetrieveEncryptionKeyCreateAlias() {
    CreateAliasRequest createAliasReq =
        new CreateAliasRequest()
            .withAliasName(String.format("alias/%s", testUniUUID.toString()))
            .withTargetKeyId(testCmkId);
    ListAliasesRequest listAliasReq = new ListAliasesRequest().withLimit(100);
    byte[] encryptionKey = awsEARService.createKey(testUniUUID, testCustomerUUID, config);
    verify(mockClient, times(1)).createKey(any(CreateKeyRequest.class));
    verify(mockClient, times(1)).listAliases(listAliasReq);
    verify(mockClient, times(1)).createAlias(createAliasReq);
    assertNotNull(encryptionKey);
    assertEquals(new String(encryptionKey), new String(mockEncryptionKey));
  }

  @Test
  @Ignore
  public void testCreateAndRetrieveEncryptionKeyUpdateAlias() {
    mockAliases.add(mockAlias);
    UpdateAliasRequest updateAliasReq =
        new UpdateAliasRequest()
            .withAliasName("alias/" + testUniUUID.toString())
            .withTargetKeyId(testCmkId);
    ListAliasesRequest listAliasReq = new ListAliasesRequest().withLimit(100);
    byte[] encryptionKey = awsEARService.createKey(testUniUUID, testCustomerUUID, config);
    verify(mockClient, times(1)).createKey(any(CreateKeyRequest.class));
    verify(mockClient, times(1)).listAliases(listAliasReq);
    verify(mockClient, times(1)).updateAlias(updateAliasReq);
    assertNotNull(encryptionKey);
    assertEquals(new String(encryptionKey), new String(mockEncryptionKey));
  }

  @Test
  public void testGetKeyMetadata() {
    // Form the expected key metadata.
    ObjectNode fakeAuthConfig = TestHelper.getFakeKmsAuthConfig(KeyProvider.AWS);
    ObjectNode expectedKeyMetadata =
        fakeAuthConfig.deepCopy().retain(Arrays.asList("AWS_REGION", "cmk_id"));
    expectedKeyMetadata.put("key_provider", KeyProvider.AWS.name());

    // Get the key metadata from the service and compare.
    KmsConfig fakeKmsConfig =
        KmsConfig.createKMSConfig(
            testCustomerUUID, testKeyProvider, fakeAuthConfig, fakeAuthConfig.get("name").asText());
    ObjectNode retrievedKeyMetadata = awsEARService.getKeyMetadata(fakeKmsConfig.getConfigUUID());
    assertEquals(expectedKeyMetadata, retrievedKeyMetadata);
  }
}
