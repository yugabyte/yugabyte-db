package com.yugabyte.yw.common.operator.helpers;

import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.databind.SerializerProvider;
import com.fasterxml.jackson.databind.ser.std.StdSerializer;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import io.yugabyte.operator.v1alpha1.ybuniversespec.kubernetesoverrides.Resource;
import java.io.IOException;
import java.util.Map;

public class KubernetesOverridesSerializer extends StdSerializer<KubernetesOverrides> {

  public KubernetesOverridesSerializer() {
    super(KubernetesOverrides.class);
  }

  @Override
  public void serialize(KubernetesOverrides value, JsonGenerator gen, SerializerProvider provider)
      throws IOException {
    Resource resource = value.getResource();
    gen.writeStartObject();
    if (value.getNodeSelector() != null) {
      provider.defaultSerializeField("nodeSelector", value.getNodeSelector(), gen);
    }
    if (value.getMaster() != null) {
      provider.defaultSerializeField("master", value.getMaster(), gen);
    }
    if (value.getTserver() != null) {
      provider.defaultSerializeField("tserver", value.getTserver(), gen);
    }
    if (value.getServiceEndpoints() != null) {
      provider.defaultSerializeField("serviceEndpoints", value.getServiceEndpoints(), gen);
    }
    if (resource != null) {
      provider.defaultSerializeField("resource", resource, gen);
    }
    if (value.getAdditionalProperties() != null) {
      for (Map.Entry<String, Object> entry : value.getAdditionalProperties().entrySet()) {
        gen.writeObjectField(entry.getKey(), entry.getValue());
      }
    }
    gen.writeEndObject();
  }
}
