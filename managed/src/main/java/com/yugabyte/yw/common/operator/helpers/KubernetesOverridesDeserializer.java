package com.yugabyte.yw.common.operator.helpers;

import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.deser.std.StdDeserializer;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import java.io.IOException;

public class KubernetesOverridesDeserializer extends StdDeserializer<KubernetesOverrides> {

  public KubernetesOverridesDeserializer() {
    super(KubernetesOverrides.class);
  }

  @Override
  public KubernetesOverrides deserialize(JsonParser p, DeserializationContext ctxt)
      throws IOException {
    JsonNode node = p.getCodec().readTree(p);
    ObjectMapper mapper = new ObjectMapper(new YAMLFactory());
    return mapper.treeToValue(node, KubernetesOverrides.class);
  }
}
