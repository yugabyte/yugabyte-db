package com.yugabyte.troubleshoot.ts.task;

import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.UniverseDetailsService;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClient;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClientError;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import lombok.Data;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.HttpStatus;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Component;

@Component
@Configuration
@Slf4j
public class UniverseDetailsQuery {

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
  public void processAllUniverses() {
    for (UniverseMetadata universeMetadata : universeMetadataService.listAll()) {
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
        universeDetailsQueryExecutor.execute(() -> processUniverse(universeMetadata, newProgress));
      } catch (Exception e) {
        log.error("Failed to schedule universe " + universeMetadata.getId(), e);
        universesProcessStartTime.remove(universeMetadata.getId());
      }
    }
  }

  private void processUniverse(UniverseMetadata metadata, UniverseProgress progress) {
    log.info("Processing universe {}", metadata.getId());
    try {
      progress.setInProgress(true);
      progress.setStartTimestamp(System.currentTimeMillis());
      UniverseDetails details = ybaClient.getUniverseDetails(metadata);
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
      }
    } catch (Exception e) {
      log.error("Failed to get universe {} details", metadata.getId(), e);
    }

    log.info("Processed universe {}", metadata.getId());
    universesProcessStartTime.remove(metadata.getId());
  }

  @Data
  @Accessors(chain = true)
  private static class UniverseProgress {
    private volatile long scheduleTimestamp;
    private volatile long startTimestamp;
    private volatile boolean inProgress = false;
  }
}
