// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

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
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import org.junit.Before;
import org.junit.Test;
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
import play.libs.Json;
import play.test.Helpers;

@RunWith(MockitoJUnitRunner.class)
public class AwsEARServiceTest extends FakeDBApplication {
    ApiHelper mockApiHelper;
    TestEncryptionAtRestService encryptionService;

    String testKeyProvider = "AWS";
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
        TestEncryptionAtRestService() {
            super(mockApiHelper, testKeyProvider);
        }

        private ObjectNode authConfig = Json.newObject()
                .put("AWS_ACCESS_KEY_ID", "some_access_key")
                .put("AWS_SECRET_ACCESS_KEY", "some_secret_key_id")
                .put("AWS_REGION", "us-west-2");

        @Override
        public ObjectNode getAuthConfig(UUID customerUUID) {
            return this.authConfig;
        }
        @Override
        protected AWSKMS getClient(UUID customerUUID) {
            return mockClient;
        }
        @Override
        public ObjectNode updateAuthConfig(UUID customerUUID, Map<String, JsonNode> newValues) {
            ObjectNode config = getAuthConfig(customerUUID);
            for (Map.Entry<String, JsonNode> newValue : newValues.entrySet()) {
                config.put(newValue.getKey(), newValue.getValue());
            }
            return config;
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
                "kms_provider", testKeyProvider,
                "algorithm", testAlgorithm,
                "key_size", Integer.toString(testKeySize),
                "cmk_policy", "some_test_policy"
        );
    }

    @Test
    public void testCreateEncryptionKeyInvalidEncryptionAlgorithm() {
        Map<String, String> testConfig = new HashMap<>(config);
        testConfig.replace("algorithm", "nonsense");
        assertNull(
                encryptionService
                        .createAndRetrieveEncryptionKey(testUniUUID, testCustomerUUID, testConfig)
        );
        verify(mockClient, times(0)).createKey(any(CreateKeyRequest.class));
    }

    @Test
    public void testCreateEncryptionKeyInvalidEncryptionKeySize() {
        Map<String, String> testConfig = new HashMap<>(config);
        testConfig.replace("key_size", "257");
        assertNull(
                encryptionService
                        .createAndRetrieveEncryptionKey(testUniUUID, testCustomerUUID, testConfig)
        );
        verify(mockClient, times(0)).createKey(any(CreateKeyRequest.class));
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeyCreateAlias() {
        CreateAliasRequest createAliasReq = new CreateAliasRequest()
                .withAliasName(String.format("alias/%s", testUniUUID.toString()))
                .withTargetKeyId(testCmkId);
        CreateKeyRequest createKeyReq = new CreateKeyRequest()
                .withDescription("Yugaware KMS Integration")
                .withPolicy(config.get("cmk_policy"));
        ListAliasesRequest listAliasReq = new ListAliasesRequest().withLimit(100);
        byte[] encryptionKey = encryptionService
                .createAndRetrieveEncryptionKey(testUniUUID, testCustomerUUID, config);
        verify(mockClient, times(1)).createKey(createKeyReq);
        verify(mockClient, times(1)).listAliases(listAliasReq);
        verify(mockClient, times(1)).createAlias(createAliasReq);
        assertEquals(new String(encryptionKey), new String(mockEncryptionKey));
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeyUpdateAlias() {
        mockAliases.add(mockAlias);
        UpdateAliasRequest updateAliasReq = new UpdateAliasRequest()
                .withAliasName("alias/" + testUniUUID.toString())
                .withTargetKeyId(testCmkId);
        CreateKeyRequest createKeyReq = new CreateKeyRequest()
                .withDescription("Yugaware KMS Integration")
                .withPolicy(config.get("cmk_policy"));
        ListAliasesRequest listAliasReq = new ListAliasesRequest().withLimit(100);
        byte[] encryptionKey = encryptionService
                .createAndRetrieveEncryptionKey(testUniUUID, testCustomerUUID, config);
        verify(mockClient, times(1)).createKey(createKeyReq);
        verify(mockClient, times(1)).listAliases(listAliasReq);
        verify(mockClient, times(1)).updateAlias(updateAliasReq);
        assertEquals(new String(encryptionKey), new String(mockEncryptionKey));
    }
}
