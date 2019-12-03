/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Test;
import play.libs.Json;
import java.util.UUID;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import static org.junit.Assert.assertEquals;

public class KmsConfigTest extends FakeDBApplication {
    @Test
    public void testCreateAndListConfig() {
        UUID customerUUID = UUID.randomUUID();
        KmsConfig config = KmsConfig.createKMSConfig(
                customerUUID,
                KeyProvider.AWS,
                Json.newObject().put("test_key", "test_val")
        );
        assertEquals(1, KmsConfig.listKMSConfigs(customerUUID).size());
    }

    @Test
    public void testUpdate() {
        UUID customerUUID = UUID.randomUUID();
        KmsConfig config = KmsConfig.createKMSConfig(
                customerUUID,
                KeyProvider.AWS,
                Json.newObject().put("test_key", "test_val")
        );
        KmsConfig.updateKMSAuthObj(
                customerUUID,
                KeyProvider.AWS,
                Json.newObject().put("test_key", "new_test_val")
        );
        KmsConfig updatedConfig = KmsConfig.getAll(customerUUID).get(0);
        assertEquals(
                updatedConfig.authConfig.get("test_key").asText(),
                "new_test_val"
        );
    }
}
