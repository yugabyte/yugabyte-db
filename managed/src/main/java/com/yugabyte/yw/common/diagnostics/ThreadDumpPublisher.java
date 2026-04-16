package com.yugabyte.yw.common.diagnostics;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigException;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.StringWriter;
import java.lang.management.ManagementFactory;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.time.Duration;
import java.util.HashMap;
import java.util.Map;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import kamon.Kamon;
import kamon.metric.Metric;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.RandomStringUtils;

@Slf4j
@Singleton
public class ThreadDumpPublisher {
  private static final Duration scheduleDelay = Duration.ofSeconds(20);
  private static final String DESTINATION_TAG = "destination";

  /** Implement this to support different destinations (S3, local volumes etc) */
  @FunctionalInterface
  interface Destination {
    boolean publish(StringWriter threadDump);
  }

  private final PlainTextThreadDumpFormatter plainTextFormatter =
      new PlainTextThreadDumpFormatter();
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final Map<String, Destination> destinations = new HashMap<>();
  private final PlatformScheduler scheduler;
  private final Metric.Counter publishFailureCounter =
      Kamon.counter(
          "yba_thread_dump_publish_failed", "Counter of failed attempts to publish a thread dump");
  private final Metric.Counter prepareFailureCounter =
      Kamon.counter(
          "yba_thread_dump_prepare_failed",
          "Counter of failed attempts to prepare a thread dump for publishing");

  @Inject
  public ThreadDumpPublisher(
      RuntimeConfigFactory runtimeConfigFactory, PlatformScheduler scheduler) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.scheduler = scheduler;

    String instance;

    try {
      instance = InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      instance = "yba-" + RandomStringUtils.randomAlphanumeric(8);
      log.error(String.format("Could not retrieve hostname, instance name set to %s", instance), e);
    }

    prepareFailureCounter.withoutTags().increment(0);

    try {
      destinations.put("gcs", new GCSDestination(runtimeConfigFactory, instance));
      publishFailureCounter.withTag(DESTINATION_TAG, "gcs").increment(0);
    } catch (ConfigException e) {
      log.error("Missing or invalid GCS config", e);
      publishFailureCounter.withTag(DESTINATION_TAG, "gcs").increment(1);
    }
  }

  public void start() {
    scheduler.schedule(
        getClass().getSimpleName(), scheduleDelay, scheduleDelay, this::collectAndPublish);
  }

  /**
   * Gets a thread dump and pushes it to all destinations enabled by the
   * yb.diag.thread_dumps.{dest}.enabled runtime config
   */
  private void collectAndPublish() {
    Config globalRuntimeConfig = runtimeConfigFactory.globalRuntimeConf();

    Map<String, Destination> enabledDestinations =
        destinations.entrySet().stream()
            .filter(
                e -> {
                  String dumpThreadsFlagPath =
                      String.format("yb.diag.thread_dumps.%s.enabled", e.getKey());
                  return (globalRuntimeConfig.hasPath(dumpThreadsFlagPath)
                      && globalRuntimeConfig.getBoolean(dumpThreadsFlagPath));
                })
            .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));

    if (enabledDestinations.isEmpty()) {
      return;
    }

    StringWriter threadDump;

    try {
      threadDump =
          plainTextFormatter.format(ManagementFactory.getThreadMXBean().dumpAllThreads(true, true));
    } catch (Exception e) {
      log.error("Failed to prepare a thread dump", e);
      prepareFailureCounter.withoutTags().increment(1);
      return;
    }

    for (Map.Entry<String, Destination> destinationEntry : enabledDestinations.entrySet()) {
      String name = destinationEntry.getKey();

      try {
        if (!destinationEntry.getValue().publish(threadDump)) {
          publishFailureCounter.withTag(DESTINATION_TAG, name).increment(1);
        }
      } catch (Exception e) {
        log.error(String.format("Failed to publish a thread dump to destination '%s'", name), e);
        publishFailureCounter.withTag(DESTINATION_TAG, name).increment(1);
      }
    }
  }
}
