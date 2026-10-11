package com.yugabyte.yw.commissioner.tasks.params;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.KmsConfig;
import java.util.UUID;

public class KMSConfigTaskParams extends AbstractTaskParams {
  public UUID customerUUID;

  public UUID configUUID;

  public String kmsConfigName;

  public KeyProvider kmsProvider;

  public ObjectNode providerConfig;

  // Set when the task originates from a KMSConfig custom resource. Its presence in the serialized
  // task params is what marks the task as operator-driven (see CustomerTaskHandler).
  public KubernetesResourceDetails kubernetesResourceDetails;

  public String getName() {
    if (kmsConfigName != null) return kmsConfigName;
    KmsConfig config = KmsConfig.get(configUUID);
    if (config != null) return config.getName();
    return String.format("%s KMS Configuration", this.kmsProvider);
  }
}
