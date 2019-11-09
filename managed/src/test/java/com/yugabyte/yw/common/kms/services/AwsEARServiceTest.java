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
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertNotNull;
import org.junit.Before;
import org.junit.Test;
import org.junit.Ignore;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import static org.mockito.Mockito.*;
import org.mockito.runners.MockitoJUnitRunner;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import play.api.Play;
import play.libs.Json;
import play.test.Helpers;

@RunWith(MockitoJUnitRunner.class)
public class AwsEARServiceTest extends FakeDBApplication {
    ApiHelper mockApiHelper;
    TestEncryptionAtRestService encryptionService;

    KeyProvider testKeyProvider = KeyProvider.AWS;
    String testAlgorithm = "AES";
    int testKeySize = 256;

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
    Map<String, String> config = null;

    byte[] mockEncryptionKey =
            new String("tcIQ6E6HJu4m3C4NbVf/1yNe/6jYi/0LAYDsIouwcnU=").getBytes();

    ByteBuffer decryptedKeyBuffer = ByteBuffer.wrap(mockEncryptionKey);

    private class TestEncryptionAtRestService extends AwsEARService {
        public boolean flipKeyRefResult = false;

        @Override
        public byte[] getKeyRef(UUID customerUUID, UUID universeUUID) {
            this.flipKeyRefResult = !this.flipKeyRefResult;
            return this.flipKeyRefResult ? null : new String("some_key_ref").getBytes();
        }
    }

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
        when(mockAlias.getAliasName())
                .thenReturn(String.format("alias/%s", testUniUUID.toString()));
        when(mockAliasList.getAliases()).thenReturn(mockAliases);
        when(mockClient.listAliases(any(ListAliasesRequest.class))).thenReturn(mockAliasList);
        when(mockClient.createKey(any(CreateKeyRequest.class))).thenReturn(mockCreateKeyResult);
        when(mockCreateKeyResult.getKeyMetadata()).thenReturn(mockKeyMetadata);
        when(mockKeyMetadata.getKeyId()).thenReturn(testCmkId);
        when(
                mockClient.generateDataKeyWithoutPlaintext(
                        any(GenerateDataKeyWithoutPlaintextRequest.class)
                )
        ).thenReturn(mockDataKeyResult);
        when(mockDataKeyResult.getCiphertextBlob())
                .thenReturn(
                        ByteBuffer.wrap(new String("some_universe_key_value_encrypted").getBytes())
                );
        when(mockClient.decrypt(any(DecryptRequest.class))).thenReturn(mockDecryptResult);
        when(mockDecryptResult.getPlaintext()).thenReturn(decryptedKeyBuffer);
        encryptionService = new TestEncryptionAtRestService();
        config = ImmutableMap.of(
                "kms_provider", testKeyProvider.name(),
                "algorithm", testAlgorithm,
                "key_size", Integer.toString(testKeySize),
                "cmk_policy", "some_test_policy"
        );
    }

    @Test
    public void testCreateEncryptionKeyInvalidEncryptionAlgorithm() {
        Map<String, String> testConfig = new HashMap<>(config);
        testConfig.replace("algorithm", "nonsense");
        assertNull(encryptionService.createKey(testUniUUID, testCustomerUUID, testConfig));
        verify(mockClient, times(0)).generateDataKeyWithoutPlaintext(
                any(GenerateDataKeyWithoutPlaintextRequest.class)
        );
    }

    @Test
    public void testCreateEncryptionKeyInvalidEncryptionKeySize() {
        Map<String, String> testConfig = new HashMap<>(config);
        testConfig.replace("key_size", "257");
        assertNull(encryptionService.createKey(testUniUUID, testCustomerUUID, testConfig));
        verify(mockClient, times(0)).generateDataKeyWithoutPlaintext(
                any(GenerateDataKeyWithoutPlaintextRequest.class)
        );
    }

    @Test
    @Ignore public void testCreateAndRetrieveEncryptionKeyCreateAlias() {
        CreateAliasRequest createAliasReq = new CreateAliasRequest()
                .withAliasName(String.format("alias/%s", testUniUUID.toString()))
                .withTargetKeyId(testCmkId);
        CreateKeyRequest createKeyReq = new CreateKeyRequest()
                .withDescription("Yugaware KMS Integration")
                .withPolicy(config.get("cmk_policy"));
        ListAliasesRequest listAliasReq = new ListAliasesRequest().withLimit(100);
        byte[] encryptionKey = encryptionService.createKey(testUniUUID, testCustomerUUID, config);
        verify(mockClient, times(1)).createKey(createKeyReq);
        verify(mockClient, times(1)).listAliases(listAliasReq);
        verify(mockClient, times(1)).createAlias(createAliasReq);
        assertNotNull(encryptionKey);
        assertEquals(new String(encryptionKey), new String(mockEncryptionKey));
    }

    @Test
    @Ignore public void testCreateAndRetrieveEncryptionKeyUpdateAlias() {
        mockAliases.add(mockAlias);
        UpdateAliasRequest updateAliasReq = new UpdateAliasRequest()
                .withAliasName("alias/" + testUniUUID.toString())
                .withTargetKeyId(testCmkId);
        CreateKeyRequest createKeyReq = new CreateKeyRequest()
                .withDescription("Yugaware KMS Integration")
                .withPolicy(config.get("cmk_policy"));
        ListAliasesRequest listAliasReq = new ListAliasesRequest().withLimit(100);
        byte[] encryptionKey = encryptionService.createKey(testUniUUID, testCustomerUUID, config);
        verify(mockClient, times(1)).createKey(createKeyReq);
        verify(mockClient, times(1)).listAliases(listAliasReq);
        verify(mockClient, times(1)).updateAlias(updateAliasReq);
        assertNotNull(encryptionKey);
        assertEquals(new String(encryptionKey), new String(mockEncryptionKey));
    }
}
