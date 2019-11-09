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

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Test;
import play.libs.Json;
import java.util.List;
import java.util.UUID;
import com.yugabyte.yw.models.KmsHistoryId.TargetType;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertNotNull;

public class KmsHistoryTest extends FakeDBApplication {
    @Test
    public void testCreateUniverseHistory() {
        UUID configUUID = UUID.randomUUID();
        UUID universeUUID = UUID.randomUUID();
        KmsHistory keyRef = KmsHistory
                .createKmsHistory(configUUID, universeUUID, TargetType.UNIVERSE_KEY, "a");
        assertNotNull(keyRef);
    }

    @Test
    public void testGetCurrentKeyRef() {
        UUID configUUID = UUID.randomUUID();
        UUID universeUUID = UUID.randomUUID();
        KmsHistory.createKmsHistory(configUUID, universeUUID, TargetType.UNIVERSE_KEY, "a");
        KmsHistory keyRef = KmsHistory
                .getCurrentKeyRef(configUUID, universeUUID, TargetType.UNIVERSE_KEY);
        assertEquals(keyRef.keyRef, "a");
    }

    @Test
    public void testGetAllTargetKeyRefs() {
        UUID configUUID = UUID.randomUUID();
        UUID universeUUID = UUID.randomUUID();
        KmsHistory.createKmsHistory(configUUID, universeUUID, TargetType.UNIVERSE_KEY, "a");
        List<KmsHistory> targetHistory = KmsHistory
                .getAllTargetKeyRefs(configUUID, universeUUID, TargetType.UNIVERSE_KEY);
        assertEquals(targetHistory.size(), 1);
    }

    @Test
    public void testDeleteAllTargetKeyRefs() {
        UUID configUUID = UUID.randomUUID();
        UUID universeUUID = UUID.randomUUID();
        KmsHistory.createKmsHistory(configUUID, universeUUID, TargetType.UNIVERSE_KEY, "a");
        KmsHistory.deleteAllTargetKeyRefs(configUUID, universeUUID, TargetType.UNIVERSE_KEY);
        List<KmsHistory> targetHistory = KmsHistory
                .getAllTargetKeyRefs(configUUID, universeUUID, TargetType.UNIVERSE_KEY);
        assertEquals(targetHistory.size(), 0);
    }
}
