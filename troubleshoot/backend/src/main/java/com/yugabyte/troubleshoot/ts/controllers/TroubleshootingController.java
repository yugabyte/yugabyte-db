package com.yugabyte.troubleshoot.ts.controllers;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.models.GraphQuery;
import com.yugabyte.troubleshoot.ts.models.GraphResponse;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import com.yugabyte.troubleshoot.ts.service.TroubleshootingService;
import java.util.List;
import java.util.UUID;
import org.springframework.web.bind.annotation.*;

@RestController
public class TroubleshootingController {

  private final TroubleshootingService troubleshootingService;
  private final GraphService graphService;
  private final ObjectMapper objectMapper;

  public TroubleshootingController(
      TroubleshootingService troubleshootingService,
      GraphService graphService,
      ObjectMapper objectMapper) {
    this.troubleshootingService = troubleshootingService;
    this.graphService = graphService;
    this.objectMapper = objectMapper;
  }

  @GetMapping("/anomalies_metadata")
  public List<Anomaly> getAnomaliesMetadata() {
    ObjectReader anomaliesMetadataReader =
        objectMapper.readerFor(new TypeReference<List<AnomalyMetadata>>() {});
    String responseStr = CommonUtils.readResource("mocks/anomalies_metadata.json");
    try {
      return anomaliesMetadataReader.readValue(responseStr);
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Failed to parse mocked response", e);
    }
  }

  @GetMapping("/anomalies")
  public List<Anomaly> findAnomalies(
      @RequestParam("universe_uuid") UUID universeUuid,
      @RequestParam(name = "mocked", required = false, defaultValue = "false") boolean mocked) {
    if (mocked) {
      ObjectReader anomaliesReader = objectMapper.readerFor(new TypeReference<List<Anomaly>>() {});
      String responseStr = CommonUtils.readResource("mocks/anomalies.json");
      try {
        return anomaliesReader.readValue(responseStr);
      } catch (JsonProcessingException e) {
        throw new RuntimeException("Failed to parse mocked response", e);
      }
    }
    return troubleshootingService.findAnomalies(universeUuid);
  }

  @PostMapping("/graphs")
  public List<GraphResponse> getGraphs(
      @RequestParam("universe_uuid") UUID universeUuid,
      @RequestParam(name = "mocked", required = false, defaultValue = "false") boolean mocked,
      @RequestBody List<GraphQuery> queries) {
    if (mocked) {
      String resourcePath = "mocks/graphs_latency_increase.json";
      if (queries.stream().anyMatch(q -> q.getName().equals("tserver_rpcs_per_sec_by_universe"))) {
        resourcePath = "mocks/graphs_cpu_distribution.json";
      }
      ObjectReader graphsReader =
          objectMapper.readerFor(new TypeReference<List<GraphResponse>>() {});
      String responseStr = CommonUtils.readResource(resourcePath);
      try {
        return graphsReader.readValue(responseStr);
      } catch (JsonProcessingException e) {
        throw new RuntimeException("Failed to parse mocked response", e);
      }
    }
    return graphService.getGraphs(universeUuid, queries);
  }
}
