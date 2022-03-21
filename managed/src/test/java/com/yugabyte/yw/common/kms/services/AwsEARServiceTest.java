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
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

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
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AwsEARServiceTest extends FakeDBApplication {
  ApiHelper mockApiHelper;
  AwsEARService encryptionService;

  KeyProvider testKeyProvider = KeyProvider.AWS;

  String testCmkId = "some_cmk_id";

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
    when(mockAlias.getAliasName()).thenReturn(String.format("alias/%s", testUniUUID.toString()));
    when(mockAliasList.getAliases()).thenReturn(mockAliases);
    when(mockClient.listAliases(any(ListAliasesRequest.class))).thenReturn(mockAliasList);
    when(mockClient.createKey(any(CreateKeyRequest.class))).thenReturn(mockCreateKeyResult);
    when(mockCreateKeyResult.getKeyMetadata()).thenReturn(mockKeyMetadata);
    when(mockKeyMetadata.getKeyId()).thenReturn(testCmkId);
    when(mockClient.generateDataKeyWithoutPlaintext(
            any(GenerateDataKeyWithoutPlaintextRequest.class)))
        .thenReturn(mockDataKeyResult);
    when(mockDataKeyResult.getCiphertextBlob())
        .thenReturn(ByteBuffer.wrap(new String("some_universe_key_value_encrypted").getBytes()));
    when(mockClient.decrypt(any(DecryptRequest.class))).thenReturn(mockDecryptResult);
    when(mockDecryptResult.getPlaintext()).thenReturn(decryptedKeyBuffer);
    encryptionService = new AwsEARService();
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
    byte[] encryptionKey = encryptionService.createKey(testUniUUID, testCustomerUUID, config);
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
    byte[] encryptionKey = encryptionService.createKey(testUniUUID, testCustomerUUID, config);
    verify(mockClient, times(1)).createKey(any(CreateKeyRequest.class));
    verify(mockClient, times(1)).listAliases(listAliasReq);
    verify(mockClient, times(1)).updateAlias(updateAliasReq);
    assertNotNull(encryptionKey);
    assertEquals(new String(encryptionKey), new String(mockEncryptionKey));
  }
}
