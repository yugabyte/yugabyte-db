// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertNotNull;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;

enum TestAlgorithm implements SupportedAlgorithmInterface {
    TEST_ALGORITHM(Arrays.asList(1, 2, 3, 4));

    private List<Integer> keySizes;

    public List<Integer> getKeySizes() {
        return this.keySizes;
    }

    private TestAlgorithm(List<Integer> keySizes) {
        this.keySizes = keySizes;
    }
}

class TestEncryptionAtRestService extends EncryptionAtRestService<TestAlgorithm> {
    public TestEncryptionAtRestService(ApiHelper apiHelper, String keyProvider) {
        super(apiHelper, keyProvider);
    }

    @Override
    protected TestAlgorithm[] getSupportedAlgorithms() { return TestAlgorithm.values(); }

    @Override
    protected String createEncryptionKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        return "some_key_id";
    }

    @Override
    protected byte[] getEncryptionKeyWithService(
            String kId, UUID customerUUID, UUID universeUUID, Map<String, String> config
    ) {
        return new String("some_key_value").getBytes();
    }

    @Override
    public byte[] recoverEncryptionKeyWithService(
            UUID customerUUID, UUID universeUUID, Map<String, String> config
    ) {
        return customerUUID == null ? new String("some_key_value").getBytes() : null;
    }

    @Override
    protected ObjectNode encryptConfigData(UUID customerUUID, ObjectNode config) {
        return config;
    }

    @Override
    protected ObjectNode decryptConfigData(UUID customerUUID, ObjectNode config) {
        return config;
    }
}

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestServiceTest {

    @Test
    public void testGetServiceNotImplemented() {
        assertNull(EncryptionAtRestService.getServiceInstance(null, "UNSUPPORTED"));
    }

    @Test
    public void testGetServiceNewInstance() {
        assertNotNull(EncryptionAtRestService.getServiceInstance(null, "SMARTKEY"));
    }

    @Test
    public void testGetServiceSingleton() {
        EncryptionAtRestService newService = EncryptionAtRestService
                .getServiceInstance(null, "SMARTKEY");
        assertEquals(
                EncryptionAtRestService.KeyProvider.SMARTKEY.getServiceInstance().hashCode(),
                newService.hashCode()
        );
    }

    @Test
    public void testGetServiceSingletonUpdateConfig() {
        EncryptionAtRestService originalService = EncryptionAtRestService
                .getServiceInstance(null, "SMARTKEY");
        EncryptionAtRestService updatedService = EncryptionAtRestService
                .getServiceInstance(null, "SMARTKEY");
        assertNotNull(EncryptionAtRestService.KeyProvider.SMARTKEY.getServiceInstance());
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeySuccess() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(null, "TEST");
        assertEquals(new String(service.createAndRetrieveEncryptionKey(
                UUID.randomUUID(),
                UUID.randomUUID(),
                ImmutableMap.of(
                        "algorithm", "TEST_ALGORITHM",
                        "key_size", "1"
                )
        )), "some_key_value");
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeyInvalidAlgorithm() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(null, "TEST");
        assertNull(service.createAndRetrieveEncryptionKey(
                UUID.randomUUID(),
                UUID.randomUUID(),
                ImmutableMap.of(
                        "algorithm", "UNSUPPORTED",
                        "key_size", "1997"
                )
        ));
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeyInvalidKeySize() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(null, "TEST");
        assertNull(service.createAndRetrieveEncryptionKey(
                UUID.randomUUID(),
                UUID.randomUUID(),
                ImmutableMap.of(
                        "algorithm", "TEST_ALGORITHM",
                        "key_size", "1997"
                )
        ));
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeyDuplicate() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(null, "TEST");
        assertNull(service.createAndRetrieveEncryptionKey(
                UUID.randomUUID(),
                null,
                ImmutableMap.of(
                        "algorithm", "TEST_ALGORITHM",
                        "key_size", "1"
                )
        ));
    }
}
