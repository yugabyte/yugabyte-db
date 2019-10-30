package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.EncryptionAtRestManager.KeyProvider;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import java.util.UUID;
import java.util.List;
import java.util.Map;

public class EncryptionAtRestKeyParams extends UniverseTaskParams {
    public UUID universeUUID;

    public Map<String, String> encryptionAtRestConfig;

    public String encryptionKeyFilePath;

    public boolean isKubernetesUniverse = false;

    public int expectedUniverseVersion = -1;

    public static EncryptionAtRestKeyParams bindFromFormData(ObjectNode formData) {
        EncryptionAtRestKeyParams params = new EncryptionAtRestKeyParams();
        String cmkPolicy = "";
        if (formData.get("cmk_policy") != null) cmkPolicy = formData.get("cmk_policy").asText();
        params.universeUUID = UUID.fromString(formData.get("universeUUID").asText());
        params.encryptionAtRestConfig = ImmutableMap.of(
                "kms_provider", formData.get("kms_provider").asText(),
                "algorithm", formData.get("algorithm").asText(),
                "key_size", formData.get("key_size").asText(),
                "cmk_policy", cmkPolicy
        );
        return params;
    }
}
