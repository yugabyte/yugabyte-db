/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertNotNull;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import org.mockito.runners.MockitoJUnitRunner;
import play.Application;
import play.api.Play;
import static play.inject.Bindings.bind;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.WithApplication;

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
    public TestEncryptionAtRestService(
            ApiHelper apiHelper,
            EncryptionAtRestManager.KeyProvider keyProvider,
            EncryptionAtRestManager util,
            boolean createRequest
    ) {
        super(apiHelper, keyProvider, util);
        this.createRequest = createRequest;
    }

    public TestEncryptionAtRestService(
            ApiHelper apiHelper,
            EncryptionAtRestManager.KeyProvider keyProvider,
            EncryptionAtRestManager util
    ) {
        this(apiHelper, keyProvider, util, false);
    }

    public boolean createRequest;

    @Override
    protected TestAlgorithm[] getSupportedAlgorithms() { return TestAlgorithm.values(); }

    @Override
    protected byte[] createKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        return "some_key_id".getBytes();
    }

    @Override
    protected byte[] rotateKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        return "some_key_id".getBytes();
    }

    @Override
    public byte[] retrieveKey(UUID customerUUID, UUID universeUUID) {
        this.createRequest = !this.createRequest;
        return this.createRequest ? null : "some_key_value".getBytes();
    }

    @Override
    public void addKeyRef(UUID customerUUID, UUID universeUUID, byte[] ref) {}
}

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestServiceTest extends WithApplication {
    EncryptionAtRestManager mockUtil;

    @Override
    protected Application provideApplication() {
        mockUtil = mock(EncryptionAtRestManager.class);
        return new GuiceApplicationBuilder()
                .overrides(bind(EncryptionAtRestManager.class).toInstance(mockUtil))
                .build();
    }

    @Before
    public void setUp() {}

    @Test
    public void testGetServiceNotImplemented() {
        assertNull(new EncryptionAtRestManager().getServiceInstance("UNSUPPORTED"));
    }

    @Test
    public void testGetServiceNewInstance() {
        assertNotNull(new EncryptionAtRestManager().getServiceInstance("SMARTKEY"));
    }

    @Test
    public void testGetServiceSingleton() {
        EncryptionAtRestService newService = new EncryptionAtRestManager()
                .getServiceInstance("SMARTKEY");
        assertEquals(
                EncryptionAtRestManager.KeyProvider.SMARTKEY.getServiceInstance().hashCode(),
                newService.hashCode()
        );
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeySuccess() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(
                null, EncryptionAtRestManager.KeyProvider.AWS, mockUtil
        );
        assertEquals(new String(service.createKey(
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
        EncryptionAtRestService service = new TestEncryptionAtRestService(
                null, EncryptionAtRestManager.KeyProvider.AWS, mockUtil
        );
        assertNull(service.createKey(
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
        EncryptionAtRestService service = new TestEncryptionAtRestService(
                null, EncryptionAtRestManager.KeyProvider.AWS, mockUtil
        );
        assertNull(service.createKey(
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
        EncryptionAtRestService service = new TestEncryptionAtRestService(
                null,
                EncryptionAtRestManager.KeyProvider.AWS,
                mockUtil,
                true
        );
        assertNull(service.createKey(
                UUID.randomUUID(),
                UUID.randomUUID(),
                ImmutableMap.of(
                        "algorithm", "TEST_ALGORITHM",
                        "key_size", "1"
                )
        ));
    }

    @Test
    public void testRotateKey() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(
                null,
                EncryptionAtRestManager.KeyProvider.AWS,
                mockUtil,
                true
        );
        byte[] key = service.rotateKey(
                UUID.randomUUID(),
                UUID.randomUUID(),
                ImmutableMap.of(
                        "algorithm", "TEST_ALGORITHM",
                        "key_size", "1"
                )
        );
        assertEquals(new String(key), "some_key_value");
    }
}
