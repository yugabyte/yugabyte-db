package com.yugabyte.troubleshoot.ts.task;

import static com.yugabyte.troubleshoot.ts.MetricsUtil.*;

import com.yugabyte.troubleshoot.ts.logs.LogsUtil;
import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.UniverseDetailsService;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClient;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClientError;
import io.prometheus.client.Summary;
import java.time.Instant;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import lombok.Data;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.apache.hc.client5.http.HttpHostConnectException;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Profile;
import org.springframework.http.HttpStatus;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Component;

@Component
@Configuration
@Slf4j
@Profile("!test")
public class UniverseDetailsQuery {
  private static final Summary QUERY_TIME =
      buildSummary(
          "ts_universe_details_query_time_millis", "Universe details query time", LABEL_RESULT);

  private final Map<UUID, UniverseProgress> universesProcessStartTime = new ConcurrentHashMap<>();
  private final UniverseMetadataService universeMetadataService;
  private final UniverseDetailsService universeDetailsService;

  private final ThreadPoolTaskExecutor universeDetailsQueryExecutor;

  private final YBAClient ybaClient;

  public UniverseDetailsQuery(
      UniverseMetadataService universeMetadataService,
      UniverseDetailsService universeDetailsService,
      ThreadPoolTaskExecutor universeDetailsQueryExecutor,
      YBAClient ybaClient) {
    this.universeMetadataService = universeMetadataService;
    this.universeDetailsService = universeDetailsService;
    this.universeDetailsQueryExecutor = universeDetailsQueryExecutor;
    this.ybaClient = ybaClient;
  }

  @Scheduled(fixedDelayString = "${task.universe_details_query.period}")
  public Map<UUID, UniverseProgress> processAllUniverses() {
    return LogsUtil.callWithContext(this::processAllUniversesInternal);
  }

  private Map<UUID, UniverseProgress> processAllUniversesInternal() {
    Map<UUID, UniverseProgress> result = new HashMap<>();
    for (UniverseMetadata universeMetadata : universeMetadataService.listAll()) {
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
      result.put(universeMetadata.getId(), progress);
      try {
        universeDetailsQueryExecutor.execute(
            LogsUtil.withUniverseId(
                () -> processUniverse(universeMetadata, newProgress), universeMetadata.getId()));
      } catch (Exception e) {
        log.error("Failed to schedule universe " + universeMetadata.getId(), e);
        universesProcessStartTime.remove(universeMetadata.getId());
      }
    }
    return result;
  }

  private void processUniverse(UniverseMetadata metadata, UniverseProgress progress) {
    log.info("Processing universe {}", metadata.getId());
    long startTime = System.currentTimeMillis();
    try {
      progress.setInProgress(true);
      progress.setStartTimestamp(System.currentTimeMillis());
      UniverseDetails details = ybaClient.getUniverseDetails(metadata);
      updateSyncStatusOnSuccess(details);
      universeDetailsService.save(details);
    } catch (YBAClientError error) {
      if (error.getStatusCode() == HttpStatus.BAD_REQUEST) {
        // This means that universe was deleted already.
        log.warn("Universe {} is missing", metadata.getId());
        universeDetailsService.delete(metadata.getId());
      } else {
        log.warn(
            "Failed to get universe {}. Status: {}. Details : {}",
            metadata.getId(),
            error.getStatusCode(),
            error.getError());
        if (error.getStatusCode() == HttpStatus.UNAUTHORIZED) {
          updateSyncStatusOnFailure(metadata, "API token expired");
        } else {
          updateSyncStatusOnFailure(metadata, "Status code" + error.getStatusCode() + " received");
        }
      }
      QUERY_TIME.labels(RESULT_SUCCESS).observe(System.currentTimeMillis() - startTime);
    } catch (Exception e) {
      if (e.getCause() instanceof HttpHostConnectException) {
        // This means connection to YBA failed
        updateSyncStatusOnFailure(metadata, "YBA is unavailable");
      } else {
        updateSyncStatusOnFailure(metadata, "Error occurred: " + e.getMessage());
      }
      QUERY_TIME.labels(RESULT_FAILURE).observe(System.currentTimeMillis() - startTime);
      log.error("Failed to get universe {} details", metadata.getId(), e);
    } finally {
      progress.setInProgress(false);
    }

    log.info("Processed universe {}", metadata.getId());
    universesProcessStartTime.remove(metadata.getId());
  }

  private void updateSyncStatusOnSuccess(UniverseDetails universeDetails) {
    Instant timestamp = Instant.now();
    universeDetails.setLastSyncStatus(true);
    universeDetails.setLastSyncTimestamp(timestamp);
    universeDetails.setLastSuccessfulSyncTimestamp(timestamp);
    universeDetails.setLastSyncError(null);
  }

  private void updateSyncStatusOnFailure(UniverseMetadata metadata, String errorMessage) {
    try {
      UniverseDetails universeDetails = universeDetailsService.get(metadata.getId());
      if (universeDetails == null) {
        return;
      }
      Instant timestamp = Instant.now();
      universeDetails.setLastSyncStatus(false);
      universeDetails.setLastSyncTimestamp(timestamp);
      universeDetails.setLastSyncError(errorMessage);
      universeDetailsService.save(universeDetails);
    } catch (Exception e) {
      log.error(
          "Failed to update universe details sync status for universe {}", metadata.getId(), e);
    }
  }

  @Data
  @Accessors(chain = true)
  public static class UniverseProgress {
    private volatile long scheduleTimestamp;
    private volatile long startTimestamp;
    private volatile boolean inProgress = false;
  }
}
