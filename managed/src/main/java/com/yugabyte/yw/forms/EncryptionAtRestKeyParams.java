/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms;

import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import java.util.UUID;
import java.util.List;
import java.util.Map;

public class EncryptionAtRestKeyParams extends UniverseTaskParams {
    public enum OpType {
        @EnumValue("ENABLE")
        ENABLE,
        @EnumValue("DISABLE")
        DISABLE;
    }

    public boolean isKubernetesUniverse = false;

    public OpType op;

    private static OpType opTypeFromString(String opTypeString) {
        return opTypeString == null ? OpType.ENABLE : OpType.valueOf(opTypeString);
    }

    public static EncryptionAtRestKeyParams bindFromFormData(ObjectNode formData) {
        EncryptionAtRestKeyParams params = new EncryptionAtRestKeyParams();
        String cmkPolicy = "";
        String keyType = "";
        String algorithm = "";
        String keySize = "";
        if (formData.get("cmk_policy") != null) cmkPolicy = formData.get("cmk_policy").asText();
        if (formData.get("key_type") != null) keyType = formData.get("key_type").asText();
        if (formData.get("algorithm") != null) algorithm = formData.get("algorithm").asText();
        if (formData.get("key_size") != null) keySize = formData.get("key_size").asText();
        params.universeUUID = UUID.fromString(formData.get("universeUUID").asText());
        params.encryptionAtRestConfig = ImmutableMap.of(
                "kms_provider", formData.get("kms_provider").asText(),
                "algorithm", algorithm,
                "key_size", keySize,
                "cmk_policy", cmkPolicy,
                "key_type", keyType
        );
        String keyOpString = null;
        if (formData.get("key_op") != null) keyOpString = formData.get("key_op").asText();
        params.op = opTypeFromString(keyOpString);
        return params;
    }
}
