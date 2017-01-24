// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Before;
import org.junit.Test;

import javax.persistence.PersistenceException;

import java.util.List;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;

public class AccessKeyTest  extends FakeDBApplication {
    Provider defaultProvider;

    @Before
    public void setUp() {
        defaultProvider = Provider.create("Provider-1", "Sample Provider");
    }

    @Test
    public void testValidCreate() {
        AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
        keyInfo.publicKey = "sample public key";
        keyInfo.privateKey = "sample private key";
        keyInfo.accessSecret = "sample access secret";
        keyInfo.accessKey = "sample access key";

        AccessKey ak = AccessKey.create(defaultProvider.uuid, "access-code1", keyInfo);
        assertNotNull(ak);
        assertThat(ak.getKeyCode(), allOf(notNullValue(), equalTo("access-code1")));
        assertThat(ak.getProviderUUID(), allOf(notNullValue(), equalTo(defaultProvider.uuid)));
        keyInfo = ak.getKeyInfo();
        assertThat(keyInfo.publicKey, allOf(notNullValue(), equalTo("sample public key")));
        assertThat(keyInfo.privateKey, allOf(notNullValue(), equalTo("sample private key")));
        assertThat(keyInfo.accessSecret, allOf(notNullValue(), equalTo("sample access secret")));
        assertThat(keyInfo.accessKey, allOf(notNullValue(), equalTo("sample access key")));
    }

    @Test
    public void testCreateWithInvalidDetails() {
        AccessKey ak = AccessKey.create(defaultProvider.uuid, "access-code1", new AccessKey.KeyInfo());
        assertNotNull(ak);
        assertThat(ak.getKeyCode(), allOf(notNullValue(), equalTo("access-code1")));
        assertThat(ak.getProviderUUID(), allOf(notNullValue(), equalTo(defaultProvider.uuid)));
        AccessKey.KeyInfo keyInfo = ak.getKeyInfo();

        assertNull(keyInfo.publicKey);
        assertNull(keyInfo.privateKey);
        assertNull(keyInfo.accessSecret);
        assertNull(keyInfo.accessKey);
    }

    @Test(expected = PersistenceException.class)
    public void testCreateWithDuplicateCode() {
        AccessKey.create(defaultProvider.uuid, "access-code1", new AccessKey.KeyInfo());
        AccessKey.create(defaultProvider.uuid, "access-code1", new AccessKey.KeyInfo());
    }


    @Test
    public void testGetValidKeyCode() {
        AccessKey.create(defaultProvider.uuid, "access-code1", new AccessKey.KeyInfo());
        AccessKey ak = AccessKey.get(defaultProvider.uuid, "access-code1");
        assertNotNull(ak);
        assertThat(ak.getKeyCode(), allOf(notNullValue(), equalTo("access-code1")));
    }

    @Test
    public void testGetInvalidKeyCode() {
        AccessKey ak = AccessKey.get(defaultProvider.uuid, "access-code1");
        assertNull(ak);
    }

    @Test
    public void testGetAllWithValidKeyCodes() {
        AccessKey.create(defaultProvider.uuid, "access-code1", new AccessKey.KeyInfo());
        AccessKey.create(defaultProvider.uuid, "access-code2", new AccessKey.KeyInfo());
        AccessKey.create(UUID.randomUUID(), "access-code3", new AccessKey.KeyInfo());
        List<AccessKey> accessKeys = AccessKey.getAll(defaultProvider.uuid);
        assertEquals(2, accessKeys.size());
    }

    @Test
    public void testGetAllWithNoKeyCodes() {
        AccessKey.create(UUID.randomUUID(), "access-code3", new AccessKey.KeyInfo());
        List<AccessKey> accessKeys = AccessKey.getAll(defaultProvider.uuid);
        assertEquals(0, accessKeys.size());
    }
}
