package com.yugabyte.troubleshoot.ts.task;

import static com.yugabyte.troubleshoot.ts.CommonUtils.PG_TIMESTAMP_FORMAT;
import static com.yugabyte.troubleshoot.ts.CommonUtils.SYSTEM_PLATFORM;
import static com.yugabyte.troubleshoot.ts.MetricsUtil.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.troubleshoot.ts.logs.LogsUtil;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.*;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClient;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClientError;
import com.yugabyte.troubleshoot.ts.yba.models.RunQueryResult;
import io.prometheus.client.Summary;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Future;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.Value;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Profile;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Component;

@Component
@Configuration
@Slf4j
@Profile("!test")
public class ActiveSessionHistoryQuery {
  private static final Summary UNIVERSE_PROCESS_TIME =
      buildSummary(
          "ts_ash_query_universe_process_time_millis",
          "Active Sessions History universe processing time",
          LABEL_RESULT);

  private static final Summary NODE_PROCESS_TIME =
      buildSummary(
          "ts_ash_query_node_process_time_millis",
          "Active Sessions History node processing time",
          LABEL_RESULT);

  static final String ASH_QUERY_NO_TIMESTAMP =
      "select sample_time, root_request_id, rpc_request_id, wait_event_component, wait_event_class,"
          + " wait_event_type, wait_event, top_level_node_id, query_id, ysql_session_id,"
          + " client_node_ip, wait_event_aux, sample_weight"
          + " from pg_catalog.yb_active_session_history";

  static final String ASH_SAMPLE_TIME_FILTER = " where sample_time >= ";

  static final String ASH_ORDER_AND_LIMIT = " order by sample_time ASC limit ";

  private static final String SAMPLE_TIME = "sample_time";
  private static final String ROOT_REQUEST_ID = "root_request_id";
  private static final String PRC_REQUEST_ID = "rpc_request_id";
  private static final String WAIT_EVENT_COMPONENT = "wait_event_component";
  private static final String WAIT_EVENT_CLASS = "wait_event_class";
  private static final String WAIT_EVENT_TYPE = "wait_event_type";
  private static final String WAIT_EVENT = "wait_event";
  private static final String TOP_LEVEL_NODE_ID = "top_level_node_id";
  private static final String QUERY_ID = "query_id";
  private static final String YSQL_SESSION_ID = "ysql_session_id";
  private static final String CLIENT_NODE_IP = "client_node_ip";
  private static final String WAIT_EVENT_AUX = "wait_event_aux";
  private static final String SAMPLE_WEIGHT = "sample_weight";

  final Map<UUID, UniverseProgress> universesProcessStartTime = new ConcurrentHashMap<>();
  private final UniverseMetadataService universeMetadataService;
  private final UniverseDetailsService universeDetailsService;
  private final ActiveSessionHistoryService activeSessionHistoryService;
  private final ActiveSessionHistoryQueryStateService ashQueryStateService;
  private final RuntimeConfigService runtimeConfigService;

  private final ThreadPoolTaskExecutor activeSessionHistoryQueryExecutor;
  private final ThreadPoolTaskExecutor activeSessionHistoryNodesQueryExecutor;

  private final YBAClient ybaClient;

  public ActiveSessionHistoryQuery(
      UniverseMetadataService universeMetadataService,
      UniverseDetailsService universeDetailsService,
      ActiveSessionHistoryService activeSessionHistoryService,
      ActiveSessionHistoryQueryStateService ashQueryStateService,
      RuntimeConfigService runtimeConfigService,
      ThreadPoolTaskExecutor activeSessionHistoryQueryExecutor,
      ThreadPoolTaskExecutor activeSessionHistoryNodesQueryExecutor,
      YBAClient ybaClient) {
    this.universeMetadataService = universeMetadataService;
    this.universeDetailsService = universeDetailsService;
    this.activeSessionHistoryService = activeSessionHistoryService;
    this.ashQueryStateService = ashQueryStateService;
    this.runtimeConfigService = runtimeConfigService;
    this.activeSessionHistoryQueryExecutor = activeSessionHistoryQueryExecutor;
    this.activeSessionHistoryNodesQueryExecutor = activeSessionHistoryNodesQueryExecutor;
    this.ybaClient = ybaClient;
  }

  @Scheduled(
      fixedRateString = "${task.active_session_history_query.period}",
      initialDelayString = "PT5S")
  public Map<UUID, UniverseProgress> processAllUniverses() {
    return LogsUtil.callWithContext(this::processAllUniversesInternal);
  }

  private Map<UUID, UniverseProgress> processAllUniversesInternal() {
    Map<UUID, UniverseProgress> result = new HashMap<>();
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
        result.put(universeMetadata.getId(), progress);
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
      result.put(universeMetadata.getId(), newProgress);
      try {
        activeSessionHistoryQueryExecutor.execute(
            LogsUtil.withUniverseId(
                () -> processUniverse(universeMetadata, details, newProgress),
                universeMetadata.getId()));
      } catch (Exception e) {
        log.error("Failed to schedule universe " + universeMetadata.getId(), e);
        universesProcessStartTime.remove(universeMetadata.getId());
      }
    }
    return result;
  }

  private void processUniverse(
      UniverseMetadata metadata, UniverseDetails details, UniverseProgress progress) {
    log.debug("Processing universe {}", details.getId());
    long startTime = System.currentTimeMillis();
    long batchSize =
        runtimeConfigService.getUniverseConfig(metadata).getLong(RuntimeConfigKey.ASH_QUERY_BATCH);
    try {
      progress.setInProgress(true);
      progress.setStartTimestamp(System.currentTimeMillis());
      progress.setNodes(details.getUniverseDetails().getNodeDetailsSet().size());
      Map<String, Future<NodeProcessResult>> results = new HashMap<>();
      Map<String, ActiveSessionHistoryQueryState> queryStates =
          ashQueryStateService.listByUniverseId(metadata.getId()).stream()
              .collect(Collectors.toMap(q -> q.getId().getNodeName(), Function.identity()));
      for (UniverseDetails.UniverseDefinition.NodeDetails node :
          details.getUniverseDetails().getNodeDetailsSet()) {
        results.put(
            node.getNodeName(),
            activeSessionHistoryNodesQueryExecutor.submit(
                LogsUtil.wrapCallable(
                    () ->
                        processNode(
                            metadata, node, batchSize, queryStates.get(node.getNodeName())))));
      }
      for (Map.Entry<String, Future<NodeProcessResult>> resultEntry : results.entrySet()) {
        try {
          NodeProcessResult result = resultEntry.getValue().get();
          if (result.isSuccess()) {
            progress.nodesSuccessful++;
          } else {
            progress.nodesFailed++;
          }
        } catch (Exception e) {
          progress.nodesFailed++;
          log.error(
              "Failed to retrieve yb_active_session_history for node {}", resultEntry.getKey(), e);
        }
      }
      universesProcessStartTime.remove(details.getId());
      UNIVERSE_PROCESS_TIME.labels(RESULT_SUCCESS).observe(System.currentTimeMillis() - startTime);
      log.info("Processed universe {}", metadata.getId());
    } catch (Exception e) {
      UNIVERSE_PROCESS_TIME.labels(RESULT_FAILURE).observe(System.currentTimeMillis() - startTime);
      log.info("Failed to process universe universe " + metadata.getId(), e);
    } finally {
      progress.inProgress = false;
    }

    universesProcessStartTime.remove(metadata.getId());
  }

  private NodeProcessResult processNode(
      UniverseMetadata metadata,
      UniverseDetails.UniverseDefinition.NodeDetails node,
      long batchSize,
      ActiveSessionHistoryQueryState queryState) {
    long startTime = System.currentTimeMillis();
    try {
      boolean fullyProcessed = false;
      while (!fullyProcessed) {
        String query = ASH_QUERY_NO_TIMESTAMP;
        if (queryState != null) {
          query += ASH_SAMPLE_TIME_FILTER + "'" + queryState.getLastSampleTime().toString() + "'";
        }
        query += ASH_ORDER_AND_LIMIT + batchSize;
        RunQueryResult result =
            ybaClient.runSqlQuery(metadata, SYSTEM_PLATFORM, query, node.getNodeName());

        List<ActiveSessionHistory> ashList = new ArrayList<>();
        Instant lastSampleTime = null;
        if (CollectionUtils.isEmpty(result.getResult())) {
          return new NodeProcessResult(true);
        }
        for (JsonNode statsJson : result.getResult()) {
          ActiveSessionHistory ashEntry =
              new ActiveSessionHistory()
                  .setUniverseId(metadata.getId())
                  .setNodeName(node.getNodeName())
                  .setSampleTime(
                      OffsetDateTime.parse(
                              statsJson.get(SAMPLE_TIME).textValue(), PG_TIMESTAMP_FORMAT)
                          .toInstant())
                  .setRootRequestId(UUID.fromString(statsJson.get(ROOT_REQUEST_ID).asText()))
                  .setWaitEventComponent(statsJson.get(WAIT_EVENT_COMPONENT).asText())
                  .setWaitEventClass(statsJson.get(WAIT_EVENT_CLASS).asText())
                  .setWaitEventType(statsJson.get(WAIT_EVENT_TYPE).asText())
                  .setWaitEvent(statsJson.get(WAIT_EVENT).asText())
                  .setTopLevelNodeId(UUID.fromString(statsJson.get(TOP_LEVEL_NODE_ID).asText()))
                  .setQueryId(statsJson.get(QUERY_ID).asLong())
                  .setYsqlSessionId(statsJson.get(YSQL_SESSION_ID).asLong())
                  .setClientNodeIp(statsJson.get(CLIENT_NODE_IP).asText())
                  .setSampleWeight(statsJson.get(SAMPLE_WEIGHT).asInt());
          JsonNode rpcRequestIdNode = statsJson.get(PRC_REQUEST_ID);
          if (rpcRequestIdNode != null && !rpcRequestIdNode.isNull()) {
            ashEntry.setRpcRequestId(rpcRequestIdNode.asLong());
          }
          JsonNode waitEventAuxNode = statsJson.get(WAIT_EVENT_AUX);
          if (waitEventAuxNode != null && !waitEventAuxNode.isNull()) {
            ashEntry.setWaitEventAux(waitEventAuxNode.asText());
          }
          if (lastSampleTime == null || lastSampleTime.isBefore(ashEntry.getSampleTime())) {
            lastSampleTime = ashEntry.getSampleTime();
          }
          ashList.add(ashEntry);
        }

        fullyProcessed = result.getResult().size() < batchSize;
        if (!fullyProcessed && !ashList.isEmpty()) {
          // We don't store last sample as we have no way to properly filter out it's entries
          // on next read. We'll store everything except last sample and re-read it next time.
          Instant sampleTimeToIgnore = lastSampleTime;
          ashList =
              ashList.stream()
                  .filter(ashEntry -> !ashEntry.getSampleTime().equals(sampleTimeToIgnore))
                  .collect(Collectors.toList());
        }
        activeSessionHistoryService.save(ashList);
        if (lastSampleTime != null) {
          ActiveSessionHistoryQueryStateId queryStateId =
              new ActiveSessionHistoryQueryStateId()
                  .setUniverseId(metadata.getId())
                  .setNodeName(node.getNodeName());
          ashQueryStateService.save(
              new ActiveSessionHistoryQueryState()
                  .setId(queryStateId)
                  .setLastSampleTime(lastSampleTime));
          queryState = ashQueryStateService.get(queryStateId);
        }
      }
      NODE_PROCESS_TIME.labels(RESULT_SUCCESS).observe(System.currentTimeMillis() - startTime);
      return new NodeProcessResult(true);
    } catch (YBAClientError error) {
      NODE_PROCESS_TIME.labels(RESULT_FAILURE).observe(System.currentTimeMillis() - startTime);
      log.warn(
          "Failed to retrieve yb_active_session_history for node {} - {}",
          node.getNodeName(),
          error.getError());
    } catch (Exception e) {
      NODE_PROCESS_TIME.labels(RESULT_FAILURE).observe(System.currentTimeMillis() - startTime);
      log.warn("Failed to retrieve yb_active_session_history for node {}", node.getNodeName(), e);
    }
    return new NodeProcessResult(false);
  }

  @Data
  @Accessors(chain = true)
  public static class UniverseProgress {
    volatile long scheduleTimestamp;
    volatile long startTimestamp;
    volatile boolean inProgress = false;
    volatile int nodes;
    volatile int nodesSuccessful;
    volatile int nodesFailed;
  }

  @Value
  private static class NodeProcessResult {
    boolean success;
  }
}
