package com.yugabyte.troubleshoot.ts.controllers;

import static org.springframework.http.HttpStatus.NOT_FOUND;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.models.GraphQuery;
import com.yugabyte.troubleshoot.ts.models.GraphResponse;
import com.yugabyte.troubleshoot.ts.service.BeanValidator;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import com.yugabyte.troubleshoot.ts.service.TroubleshootingService;
import com.yugabyte.troubleshoot.ts.service.anomaly.AnomalyMetadataProvider;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;

@RestController
public class TroubleshootingController {

  private final TroubleshootingService troubleshootingService;
  private final GraphService graphService;
  private final ObjectMapper objectMapper;
  private final BeanValidator beanValidator;

  private final Map<UUID, Map<UUID, Anomaly>> lastAnomalies = new ConcurrentHashMap<>();

  public TroubleshootingController(
      TroubleshootingService troubleshootingService,
      GraphService graphService,
      ObjectMapper objectMapper,
      BeanValidator beanValidator,
      AnomalyMetadataProvider anomalyMetadataProvider) {
    this.troubleshootingService = troubleshootingService;
    this.graphService = graphService;
    this.objectMapper = objectMapper;
    this.beanValidator = beanValidator;
  }

  @GetMapping("/anomalies_metadata")
  public List<AnomalyMetadata> getAnomaliesMetadata() {
    return troubleshootingService.getAnomaliesMetadata();
  }

  @GetMapping("/anomalies")
  public List<Anomaly> findAnomalies(
      @RequestParam("universe_uuid") UUID universeUuid,
      @RequestParam(name = "startTime", required = false) Instant startTime,
      @RequestParam(name = "endTime", required = false) Instant endTime,
      @RequestParam(name = "mocked", required = false, defaultValue = "false") boolean mocked) {
    List<Anomaly> result;
    if (mocked) {
      ObjectReader anomaliesReader = objectMapper.readerFor(new TypeReference<List<Anomaly>>() {});
      String responseStr = CommonUtils.readResource("mocks/anomalies.json");
      try {
        result = anomaliesReader.readValue(responseStr);
      } catch (JsonProcessingException e) {
        throw new RuntimeException("Failed to parse mocked response", e);
      }
    } else {
      Instant now = Instant.now();
      if (endTime == null) {
        endTime = now;
      }
      if (startTime == null) {
        startTime = now.minus(14, ChronoUnit.DAYS);
      }
      if (endTime.isBefore(startTime)) {
        beanValidator.error().global("startTime should be before endTime");
      }
      result = troubleshootingService.findAnomalies(universeUuid, startTime, endTime);
    }
    Map<UUID, Anomaly> lastUniverseAnomalies =
        result.stream().collect(Collectors.toMap(Anomaly::getUuid, Function.identity()));
    lastAnomalies.put(universeUuid, lastUniverseAnomalies);
    return result;
  }

  @GetMapping("/anomalies/{id}")
  public Anomaly getAnomaly(
      @PathVariable("id") UUID anomalyUuid, @RequestParam("universe_uuid") UUID universeUuid) {
    Map<UUID, Anomaly> lastUniverseAnomalies = lastAnomalies.get(universeUuid);
    if (lastUniverseAnomalies == null) {
      throw new ResponseStatusException(NOT_FOUND, "Anomaly not found");
    }
    Anomaly anomaly = lastUniverseAnomalies.get(anomalyUuid);
    if (anomaly == null) {
      throw new ResponseStatusException(NOT_FOUND, "Anomaly not found");
    }
    return anomaly;
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
