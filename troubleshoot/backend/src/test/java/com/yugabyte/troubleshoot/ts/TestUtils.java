package com.yugabyte.troubleshoot.ts;

import static com.yugabyte.troubleshoot.ts.CommonUtils.readResource;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import lombok.SneakyThrows;
import net.sf.jsefa.Deserializer;
import net.sf.jsefa.csv.CsvIOFactory;
import net.sf.jsefa.csv.config.CsvConfiguration;
import org.springframework.core.io.ClassPathResource;

public class TestUtils {

  private static ObjectMapper objectMapper = new ObjectMapper();

  @SneakyThrows
  public static JsonNode readResourceAsJson(String path) {
    String resourceStr = readResource(path);
    return objectMapper.readTree(resourceStr);
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

  @SneakyThrows
  public static <T> List<T> readResourceAsList(ObjectMapper mapper, String path, Class<T> clazz) {
    String resourceStr = readResource(path);
    return mapper.readerForListOf(clazz).readValue(resourceStr);
  }

  @SneakyThrows
  public static <T> List<T> readCsvAsObjects(String path, Class<T> objectClass) {
    CsvConfiguration csvConfiguration = new CsvConfiguration();
    csvConfiguration.setFieldDelimiter(',');
    Deserializer deserializer =
        CsvIOFactory.createFactory(csvConfiguration, objectClass).createDeserializer();
    try {
      deserializer.open(new InputStreamReader(new ClassPathResource(path).getInputStream()));
      List<T> result = new ArrayList<>();
      while (deserializer.hasNext()) {
        T object = deserializer.next();
        result.add(object);
      }
      return result;
    } finally {
      deserializer.close(true);
    }
  }
}
