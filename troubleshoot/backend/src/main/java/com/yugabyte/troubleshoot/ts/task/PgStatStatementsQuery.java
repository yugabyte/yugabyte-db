package com.yugabyte.troubleshoot.ts.task;

import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.UniverseMetadataService;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import lombok.Data;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.annotation.Configuration;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Component;

@Component
@Configuration
@Slf4j
public class PgStatStatementsQuery {

  private final Map<UUID, UniverseProgress> universesProcessStartTime = new ConcurrentHashMap<>();
  private final UniverseMetadataService universeMetadataService;

  private final ThreadPoolTaskExecutor pgStatStatementsQueryExecutor;

  public PgStatStatementsQuery(
      UniverseMetadataService universeMetadataService,
      ThreadPoolTaskExecutor pgStatStatementsQueryExecutor) {
    this.universeMetadataService = universeMetadataService;
    this.pgStatStatementsQueryExecutor = pgStatStatementsQueryExecutor;
  }

  @Scheduled(fixedRateString = "${task.pg_stat_statements_query.period}")
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
        pgStatStatementsQueryExecutor.execute(() -> processUniverse(universeMetadata, newProgress));
      } catch (Exception e) {
        log.error("Failed to schedule universe " + universeMetadata.getId(), e);
        universesProcessStartTime.remove(universeMetadata.getId());
      }
    }
  }

  private void processUniverse(UniverseMetadata metadata, UniverseProgress progress) {
    log.info("Processing universe {}", metadata.getId());
    // TODO
    universesProcessStartTime.remove(metadata.getId());
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
