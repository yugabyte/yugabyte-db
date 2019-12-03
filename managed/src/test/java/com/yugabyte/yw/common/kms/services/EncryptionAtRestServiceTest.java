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

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.KeyProvider;
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
import play.libs.Json;
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
            KeyProvider keyProvider,
            EncryptionAtRestManager util,
            boolean createRequest
    ) {
        super(keyProvider);
        this.createRequest = createRequest;
    }

    public TestEncryptionAtRestService(
            ApiHelper apiHelper,
            KeyProvider keyProvider,
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
    public byte[] retrieveKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            byte[] keyRef,
            Map<String, String> config
    ) {
        this.createRequest = !this.createRequest;
        return this.createRequest ? null : "some_key_value".getBytes();
    }
}

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestServiceTest extends WithApplication {
    EncryptionAtRestManager mockUtil;

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
                KeyProvider.SMARTKEY.getServiceInstance().hashCode(),
                newService.hashCode()
        );
    }

    @Test
    public void testCreateAndRetrieveEncryptionKeyInvalidAlgorithm() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(
                null, KeyProvider.AWS, mockUtil
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
                null, KeyProvider.AWS, mockUtil
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
                KeyProvider.SMARTKEY,
                mockUtil,
                false
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
    public void testCreateKey() {
        EncryptionAtRestService service = new TestEncryptionAtRestService(
                null,
                KeyProvider.AWS,
                mockUtil,
                true
        );
        UUID customerUUID = UUID.randomUUID();
        UUID universeUUID = UUID.randomUUID();
        service.createAuthConfig(customerUUID, Json.newObject().put("some_key", "some_value"));
        Map<String, String> config = ImmutableMap.of(
                "algorithm", "TEST_ALGORITHM",
                "key_size", "1"
        );
        byte[] key = service.createKey(universeUUID, customerUUID, config);
        assertNotNull(key);
        assertEquals("some_key_value", new String(key));
    }
}
