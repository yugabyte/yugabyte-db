package com.yugabyte.yw.common.diagnostics;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigException;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.StringWriter;
import java.lang.management.ManagementFactory;
import java.lang.management.ThreadInfo;
import java.lang.management.ThreadMXBean;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.time.Duration;
import java.time.OffsetDateTime;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
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
public class ThreadDumpPublisher extends BasePublisher<StringWriter> {
  private static final Duration scheduleDelay = Duration.ofSeconds(20);

  private final PlainTextThreadDumpFormatter plainTextFormatter =
      new PlainTextThreadDumpFormatter();
  private final PlatformScheduler scheduler;
  private final String basePath;
  private final Metric.Counter prepareFailureCounter;

  @Inject
  public ThreadDumpPublisher(
      RuntimeConfigFactory runtimeConfigFactory, PlatformScheduler scheduler) {
    super(runtimeConfigFactory, "yba_thread_dump");
    this.scheduler = scheduler;

    prepareFailureCounter =
        Kamon.counter(
            "yba_thread_dump_prepare_failed",
            "Counter of failed attempts to prepare content for publishing");

    ThreadMXBean threadMXBean = ManagementFactory.getThreadMXBean();
    if (threadMXBean.isThreadCpuTimeSupported() && !threadMXBean.isThreadCpuTimeEnabled()) {
      threadMXBean.setThreadCpuTimeEnabled(true);
    }

    String instance;
    try {
      instance = InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      instance = "yba-" + RandomStringUtils.randomAlphanumeric(8);
      log.error(String.format("Could not retrieve hostname, instance name set to %s", instance), e);
    }

    Config staticConf = runtimeConfigFactory.staticApplicationConf();
    String namespace =
        staticConf.hasPath("yb.diag.namespace") ? staticConf.getString("yb.diag.namespace") : null;
    basePath =
        String.join(
            "/", "thread-dumps", "yba", namespace == null ? instance : namespace + "/" + instance);
    initializeDestinations();
  }

  @Override
  protected void createDestinations() {
    try {
      destinations.put("gcs", new GCSDestination(runtimeConfigFactory));
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
    StringWriter threadDump;

    try {
      ThreadMXBean threadMXBean = ManagementFactory.getThreadMXBean();
      ThreadInfo[] threads = threadMXBean.dumpAllThreads(true, true);
      Map<Long, Long> cpuTimes =
          Arrays.stream(threads)
              .collect(
                  Collectors.toMap(
                      ThreadInfo::getThreadId,
                      t -> threadMXBean.getThreadCpuTime(t.getThreadId())));
      threadDump = plainTextFormatter.format(threads, cpuTimes);
    } catch (Exception e) {
      log.error("Failed to prepare a thread dump", e);
      prepareFailureCounter.withoutTags().increment(1);
      return;
    }

    OffsetDateTime now = OffsetDateTime.now(ZoneId.of("UTC"));
    String blobPath =
        String.join(
            "/",
            basePath,
            DateTimeFormatter.ISO_LOCAL_DATE.format(now),
            now.getHour() + ":00",
            DateTimeFormatter.ISO_LOCAL_TIME.format(now));

    publishToEnabledDestinations(threadDump, blobPath);
  }
}
