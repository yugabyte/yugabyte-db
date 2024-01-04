package com.yugabyte.troubleshoot.ts.task;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.PgStatStatements;
import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.PgStatStatementsService;
import com.yugabyte.troubleshoot.ts.service.UniverseDetailsService;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClient;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClientError;
import com.yugabyte.troubleshoot.ts.yba.models.RunQueryResult;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Future;
import lombok.Data;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Profile;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Component;

@Component
@Configuration
@Slf4j
@Profile("!test")
public class PgStatStatementsQuery {

  public static final String MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT_2_18 =
      "2.18.1.0-b67";

  /** YBDB versions above this threshold support latency histogram. */
  public static final String MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT = "2.19.1.0-b80";

  private static final String SYSTEM_PLATFORM = "system_platform";

  private static final String PG_STAT_STATEMENTS_QUERY_PART1 =
      "select now() as timestamp, dbid, datname, queryid, query, calls, total_time, rows";
  private static final String PG_STAT_STATEMENTS_QUERY_LH = ", yb_latency_histogram";
  private static final String PG_STAT_STATEMENTS_QUERY_PART2 =
      " from pg_catalog.pg_stat_statements left join pg_stat_database on dbid = datid"
          + " where datname not in ('template0', 'template1', 'system_platform')";

  private static final String TIMESTAMP = "timestamp";
  private static final String DB_ID = "dbid";
  private static final String DB_NAME = "datname";
  private static final String QUERY_ID = "queryid";
  private static final String QUERY = "query";
  private static final String CALLS = "calls";
  private static final String TOTAL_TIME = "total_time";
  private static final String ROWS = "rows";
  private static final String YB_LATENCY_HISTOGRAM = "yb_latency_histogram";

  private static final DateTimeFormatter TIMESTAMP_FORMAT =
      DateTimeFormatter.ofPattern("uuuu-MM-dd'T'HH:mm:ss.SSSSSSxxx");

  private final Map<UUID, UniverseProgress> universesProcessStartTime = new ConcurrentHashMap<>();
  private final UniverseMetadataService universeMetadataService;
  private final UniverseDetailsService universeDetailsService;
  private final PgStatStatementsService pgStatStatementsService;

  private final ThreadPoolTaskExecutor pgStatStatementsQueryExecutor;
  private final ThreadPoolTaskExecutor pgStatStatementsNodesQueryExecutor;

  private final YBAClient ybaClient;

  public PgStatStatementsQuery(
      UniverseMetadataService universeMetadataService,
      UniverseDetailsService universeDetailsService,
      PgStatStatementsService pgStatStatementsService,
      ThreadPoolTaskExecutor pgStatStatementsQueryExecutor,
      ThreadPoolTaskExecutor pgStatStatementsNodesQueryExecutor,
      YBAClient ybaClient) {
    this.universeMetadataService = universeMetadataService;
    this.universeDetailsService = universeDetailsService;
    this.pgStatStatementsService = pgStatStatementsService;
    this.pgStatStatementsQueryExecutor = pgStatStatementsQueryExecutor;
    this.pgStatStatementsNodesQueryExecutor = pgStatStatementsNodesQueryExecutor;
    this.ybaClient = ybaClient;
  }

  @Scheduled(
      fixedRateString = "${task.pg_stat_statements_query.period}",
      initialDelayString = "PT5S")
  @Profile("!test")
  public void processAllUniverses() {
    for (UniverseMetadata universeMetadata : universeMetadataService.listAll()) {
      UniverseDetails details = universeDetailsService.get(universeMetadata.getId());
      if (details == null) {
        log.warn("Universe details are missing for universe {}", universeMetadata.getId());
        continue;
      }
      if (details.getUniverseDetails().isUniversePaused()) {
        log.debug("Universe {} is paused", universeMetadata.getId());
        continue;
      }
      UniverseProgress progress = universesProcessStartTime.get(universeMetadata.getId());
      if (progress != null) {
        long scheduledMillisAgo = System.currentTimeMillis() - progress.scheduleTimestamp;
        log.warn(
            "Universe {} is scheduled {} millis ago. Current status: {}",
            universeMetadata.getId(),
            scheduledMillisAgo,
            progress);
        continue;
      }
      UniverseProgress newProgress =
          new UniverseProgress().setScheduleTimestamp(System.currentTimeMillis());
      universesProcessStartTime.put(universeMetadata.getId(), newProgress);
      try {
        pgStatStatementsQueryExecutor.execute(
            () -> processUniverse(universeMetadata, details, newProgress));
      } catch (Exception e) {
        log.error("Failed to schedule universe " + universeMetadata.getId(), e);
        universesProcessStartTime.remove(universeMetadata.getId());
      }
    }
  }

  private void processUniverse(
      UniverseMetadata metadata, UniverseDetails details, UniverseProgress progress) {
    log.debug("Processing universe {}", details.getId());
    progress.setInProgress(true);
    progress.setStartTimestamp(System.currentTimeMillis());
    progress.setNodes(details.getUniverseDetails().getNodeDetailsSet().size());
    Map<String, Future<Boolean>> results = new HashMap<>();
    for (UniverseDetails.UniverseDefinition.NodeDetails node :
        details.getUniverseDetails().getNodeDetailsSet()) {
      results.put(
          node.getNodeName(),
          pgStatStatementsNodesQueryExecutor.submit(
              () -> processNode(metadata, details, node, progress)));
    }
    for (Map.Entry<String, Future<Boolean>> resultEntry : results.entrySet()) {
      try {
        boolean isSucceeded = resultEntry.getValue().get();
        if (isSucceeded) {
          progress.nodesSuccessful++;
        } else {
          progress.nodesFailed++;
        }
      } catch (Exception e) {
        progress.nodesFailed++;
        log.error("Failed to retrieve pg_stat_statements for node {}", resultEntry.getKey(), e);
      }
    }
    universesProcessStartTime.remove(details.getId());
  }

  private boolean processNode(
      UniverseMetadata metadata,
      UniverseDetails details,
      UniverseDetails.UniverseDefinition.NodeDetails node,
      UniverseProgress progress) {
    try {
      String ybSoftwareVersion =
          details.getUniverseDetails().getClusters().stream()
              .filter(cl -> cl.getUuid().equals(node.getPlacementUuid()))
              .map(UniverseDetails.UniverseDefinition.Cluster::getUserIntent)
              .map(UniverseDetails.UniverseDefinition.UserIntent::getYbSoftwareVersion)
              .findFirst()
              .orElse(null);
      boolean latencyHistogramSupported = false;
      if (ybSoftwareVersion == null) {
        log.warn("Unknown YB software version for node {}", node.getNodeName());
      } else {
        latencyHistogramSupported =
            CommonUtils.isReleaseEqualOrAfter(
                    MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT, ybSoftwareVersion)
                || CommonUtils.isReleaseBetween(
                    MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT_2_18,
                    "2.19.0.0_b0",
                    ybSoftwareVersion);
      }

      String query = PG_STAT_STATEMENTS_QUERY_PART1;
      if (latencyHistogramSupported) {
        query += PG_STAT_STATEMENTS_QUERY_LH;
      }
      query += PG_STAT_STATEMENTS_QUERY_PART2;
      RunQueryResult result =
          ybaClient.runSqlQuery(metadata, SYSTEM_PLATFORM, query, node.getNodeName());

      List<PgStatStatements> statStatementsList = new ArrayList<>();
      for (JsonNode statsJson : result.getResult()) {
        PgStatStatements statStatements =
            new PgStatStatements()
                .setCustomerId(metadata.getCustomerId())
                .setUniverseId(metadata.getId())
                .setClusterId(node.getPlacementUuid())
                .setNodeName(node.getNodeName())
                .setCloud(node.getCloudInfo().getCloud())
                .setRegion(node.getCloudInfo().getRegion())
                .setAz(node.getCloudInfo().getAz())
                .setActualTimestamp(
                    OffsetDateTime.parse(statsJson.get(TIMESTAMP).textValue(), TIMESTAMP_FORMAT)
                        .toInstant())
                .setScheduledTimestamp(Instant.ofEpochMilli(progress.scheduleTimestamp))
                .setDbId(statsJson.get(DB_ID).asText())
                .setDbName(statsJson.get(DB_NAME).asText())
                .setQueryId(statsJson.get(QUERY_ID).asLong())
                .setQuery(statsJson.get(QUERY).asText())
                .setCalls(statsJson.get(CALLS).asLong())
                .setTotalTime(statsJson.get(TOTAL_TIME).asDouble())
                .setRows(statsJson.get(ROWS).asLong());
        if (statsJson.has(YB_LATENCY_HISTOGRAM)) {
          statStatements.setYbLatencyHistogram(statsJson.get(YB_LATENCY_HISTOGRAM));
        }
        statStatementsList.add(statStatements);
      }
      pgStatStatementsService.save(statStatementsList);
      return true;
    } catch (YBAClientError error) {
      log.warn(
          "Failed to retrieve pg_stat_statements for node {} - {}",
          node.getNodeName(),
          error.getError());
    } catch (Exception e) {
      log.warn("Failed to retrieve pg_stat_statements for node {}", node.getNodeName(), e);
    }
    return false;
  }

  @Data
  @Accessors(chain = true)
  private static class UniverseProgress {
    private volatile long scheduleTimestamp;
    private volatile long startTimestamp;
    private volatile boolean inProgress = false;
    private volatile int nodes;
    private volatile int nodesSuccessful;
    private volatile int nodesFailed;
  }
}
