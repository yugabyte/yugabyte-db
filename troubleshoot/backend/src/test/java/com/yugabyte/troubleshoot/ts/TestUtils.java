package com.yugabyte.troubleshoot.ts;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;
import lombok.SneakyThrows;
import org.springframework.core.io.ClassPathResource;
import org.springframework.util.StreamUtils;

public class TestUtils {
  @SneakyThrows
  public static String readResource(String path) {
    return StreamUtils.copyToString(
        new ClassPathResource(path).getInputStream(), Charset.defaultCharset());
  }

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
