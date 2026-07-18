package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Getter;
import org.springframework.stereotype.Component;

@Component
public class AnomalyMetadataProvider {

  @Getter private final List<AnomalyMetadata> metadataList;
  private final Map<AnomalyMetadata.AnomalyType, AnomalyMetadata> metadataMap;

  public AnomalyMetadataProvider(ObjectMapper objectMapper) {
    ObjectReader anomaliesMetadataReader =
        objectMapper.readerFor(new TypeReference<List<AnomalyMetadata>>() {});
    String responseStr = CommonUtils.readResource("anomaly/anomalies_metadata.json");
    try {
      metadataList = anomaliesMetadataReader.readValue(responseStr);
      metadataMap =
          metadataList.stream()
              .collect(Collectors.toMap(AnomalyMetadata::getType, Function.identity()));
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Failed to parse mocked response", e);
    }
  }

  public AnomalyMetadata getMetadata(AnomalyMetadata.AnomalyType type) {
    return metadataMap.get(type);
  }
}
