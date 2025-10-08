package com.yugabyte.yw.common.diagnostics;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.management.LockInfo;
import java.lang.management.ManagementFactory;
import java.lang.management.MonitorInfo;
import java.lang.management.RuntimeMXBean;
import java.lang.management.ThreadInfo;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Formats a thread dump as plain text. Originally from <a
 * href="https://github.com/spring-projects/spring-boot/blob/main/module/spring-boot-actuator/src/main/java/org/springframework/boot/actuate/management/PlainTextThreadDumpFormatter.java">Spring
 * Boot</a> The only difference from the original implementation is {@link
 * PlainTextThreadDumpFormatter#format(ThreadInfo[], Map)} accepting a map of TID -> CPU times and
 * returning a {@link StringWriter} instance so it can be streamed directly without string
 * conversion
 */
class PlainTextThreadDumpFormatter {

  StringWriter format(ThreadInfo[] threads, Map<Long, Long> cpuTimes) {
    StringWriter dump = new StringWriter();
    PrintWriter writer = new PrintWriter(dump);
    writePreamble(writer);
    for (ThreadInfo info : threads) {
      writeThread(writer, info, cpuTimes.get(info.getThreadId()));
    }
    return dump;
  }

  private void writePreamble(PrintWriter writer) {
    DateTimeFormatter dateFormat = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
    writer.println(dateFormat.format(LocalDateTime.now()));
    RuntimeMXBean runtime = ManagementFactory.getRuntimeMXBean();
    writer.printf(
        "Full thread dump %s (%s %s):%n",
        runtime.getVmName(), runtime.getVmVersion(), System.getProperty("java.vm.info"));
    writer.println();
  }

  private void writeThread(PrintWriter writer, ThreadInfo info, Long cpuTime) {
    writer.printf("\"%s\" - Thread t@%d%n", info.getThreadName(), info.getThreadId());

    if (cpuTime != null) {
      writer.printf(
          "   %s: %s (time: %d)%n",
          Thread.State.class.getCanonicalName(), info.getThreadState(), cpuTime);
    } else {
      writer.printf("   %s: %s%n", Thread.State.class.getCanonicalName(), info.getThreadState());
    }

    writeStackTrace(writer, info, info.getLockedMonitors());
    writer.println();
    writeLockedOwnableSynchronizers(writer, info);
    writer.println();
  }

  private void writeStackTrace(PrintWriter writer, ThreadInfo info, MonitorInfo[] lockedMonitors) {
    int depth = 0;
    for (StackTraceElement element : info.getStackTrace()) {
      writeStackTraceElement(
          writer, element, info, lockedMonitorsForDepth(lockedMonitors, depth), depth == 0);
      depth++;
    }
  }

  private List<MonitorInfo> lockedMonitorsForDepth(MonitorInfo[] lockedMonitors, int depth) {
    return Stream.of(lockedMonitors)
        .filter((lockedMonitor) -> lockedMonitor.getLockedStackDepth() == depth)
        .collect(Collectors.toList());
  }

  private void writeStackTraceElement(
      PrintWriter writer,
      StackTraceElement element,
      ThreadInfo info,
      List<MonitorInfo> lockedMonitors,
      boolean firstElement) {
    writer.printf("\tat %s%n", element.toString());
    LockInfo lockInfo = info.getLockInfo();
    if (firstElement && lockInfo != null) {
      if (element.getClassName().equals(Object.class.getName())
          && element.getMethodName().equals("wait")) {
        writer.printf("\t- waiting on %s%n", format(lockInfo));
      } else {
        String lockOwner = info.getLockOwnerName();
        if (lockOwner != null) {
          writer.printf(
              "\t- waiting to lock %s owned by \"%s\" t@%d%n",
              format(lockInfo), lockOwner, info.getLockOwnerId());
        } else {
          writer.printf("\t- parking to wait for %s%n", format(lockInfo));
        }
      }
    }
    writeMonitors(writer, lockedMonitors);
  }

  private String format(LockInfo lockInfo) {
    return String.format("<%x> (a %s)", lockInfo.getIdentityHashCode(), lockInfo.getClassName());
  }

  private void writeMonitors(PrintWriter writer, List<MonitorInfo> lockedMonitorsAtCurrentDepth) {
    for (MonitorInfo lockedMonitor : lockedMonitorsAtCurrentDepth) {
      writer.printf("\t- locked %s%n", format(lockedMonitor));
    }
  }

  private void writeLockedOwnableSynchronizers(PrintWriter writer, ThreadInfo info) {
    writer.println("   Locked ownable synchronizers:");
    LockInfo[] lockedSynchronizers = info.getLockedSynchronizers();
    if (lockedSynchronizers == null || lockedSynchronizers.length == 0) {
      writer.println("\t- None");
    } else {
      for (LockInfo lockedSynchronizer : lockedSynchronizers) {
        writer.printf("\t- Locked %s%n", format(lockedSynchronizer));
      }
    }
  }
}
