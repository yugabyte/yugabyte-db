package com.yugabyte.troubleshoot.ts;

import static com.yugabyte.troubleshoot.ts.CommonUtils.readResource;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import java.util.ArrayList;
import java.util.List;
import lombok.SneakyThrows;

public class TestUtils {

  @SneakyThrows
  public static JsonNode readResourceAsJson(String path) {
    String resourceStr = readResource(path);
    return new ObjectMapper().readTree(resourceStr);
  }

  @SneakyThrows
  public static List<JsonNode> readResourceAsJsonList(String path) {
    JsonNode node = readResourceAsJson(path);
    ArrayNode arrayNode = (ArrayNode) node;
    List<JsonNode> result = new ArrayList<>();
    for (JsonNode jsonNode : arrayNode) {
      result.add(jsonNode);
    }
    return result;
  }
}
