package com.yugabyte.yw.commissioner;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.time.Duration;
import java.util.List;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import scala.concurrent.ExecutionContext;

@Singleton
@Slf4j
public class TaskGarbageCollector {

  // Counter names
  static final String CUSTOMER_TASK_METRIC_NAME = "ybp_customer_task_gc_count";
  static final String TASK_INFO_METRIC_NAME = "ybp_task_info_gc_count";
  static final String NUM_TASK_GC_RUNS = "ybp_task_gc_run_count";
  static final String NUM_TASK_GC_ERRORS = "ybp_task_gc_error_count";

  // Counter label
  static final String CUSTOMER_UUID_LABEL = "customer_uuid";

  // Counters
  private static Counter PURGED_CUSTOMER_TASK_COUNT;
  private static Counter PURGED_TASK_INFO_COUNT;
  private static Counter NUM_TASK_GC_RUNS_COUNT;
  private static Counter NUM_TASK_GC_ERRORS_COUNT;

  // Config names
  static final String YB_TASK_GC_GC_CHECK_INTERVAL = "yb.taskGC.gc_check_interval";
  static final String YB_TASK_GC_TASK_RETENTION_DURATION = "yb.taskGC.task_retention_duration";

  static {
    registerMetrics();
  }

  private final Scheduler scheduler;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final ExecutionContext executionContext;

  @Inject
  public TaskGarbageCollector(
      ActorSystem actorSystem,
      RuntimeConfigFactory runtimeConfigFactory,
      ExecutionContext executionContext) {

    this.scheduler = actorSystem.scheduler();
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.executionContext = executionContext;
  }

  @VisibleForTesting
  static void registerMetrics() {
    PURGED_CUSTOMER_TASK_COUNT =
        Counter.build(
                CUSTOMER_TASK_METRIC_NAME,
                "Number of old completed customer tasks purged for a customer")
            .labelNames(CUSTOMER_UUID_LABEL)
            .register(CollectorRegistry.defaultRegistry);
    PURGED_TASK_INFO_COUNT =
        Counter.build(TASK_INFO_METRIC_NAME, "Number of tasks info rows purged for a customer")
            .labelNames(CUSTOMER_UUID_LABEL)
            .register(CollectorRegistry.defaultRegistry);
    NUM_TASK_GC_RUNS_COUNT =
        Counter.build(NUM_TASK_GC_RUNS, "Number of times customer gc checks are run")
            .register(CollectorRegistry.defaultRegistry);
    NUM_TASK_GC_ERRORS_COUNT =
        Counter.build(NUM_TASK_GC_ERRORS, "Number of failed customer_task delete attempts")
            .register(CollectorRegistry.defaultRegistry);
  }

  public void start() {
    Duration gcInterval = this.gcCheckInterval();
    if (gcInterval.isZero()) {
      log.info("yb.taskGC.gc_check_interval set to 0.");
      log.warn("!!! TASK GC DISABLED !!!");
    } else {
      log.info("Scheduling TaskGC every " + gcInterval);
      scheduler.schedule(
          Duration.ZERO, // InitialDelay
          gcInterval,
          this::scheduleRunner,
          this.executionContext);
    }
  }

  private void scheduleRunner() {
    try {
      Customer.getAll().forEach(this::checkCustomer);
    } catch (Exception e) {
      log.error("Error running task garbage collector", e);
    }
  }

  private void checkCustomer(Customer c) {
    List<CustomerTask> staleTasks = CustomerTask.findOlderThan(c, taskRetentionDuration(c));
    purgeStaleTasks(c, staleTasks);
  }

  @VisibleForTesting
  void purgeStaleTasks(Customer c, List<CustomerTask> staleTasks) {
    NUM_TASK_GC_RUNS_COUNT.inc();
    int numRowsGCdInThisRun = 0;
    for (CustomerTask customerTask : staleTasks) {
      int numRowsDeleted = customerTask.cascadeDeleteCompleted();
      numRowsGCdInThisRun += numRowsDeleted;
      if (numRowsDeleted > 0) {
        PURGED_CUSTOMER_TASK_COUNT.labels(c.getUuid().toString()).inc();
        PURGED_TASK_INFO_COUNT.labels(c.getUuid().toString()).inc(numRowsDeleted - 1);
      } else {
        NUM_TASK_GC_ERRORS_COUNT.inc();
      }
    }
    log.info("Garbage collected {} rows", numRowsGCdInThisRun);
  }

  /** The interval at which the gc checker will run. */
  private Duration gcCheckInterval() {
    return runtimeConfigFactory.staticApplicationConf().getDuration(YB_TASK_GC_GC_CHECK_INTERVAL);
  }

  /** For how many days to retain a completed task before garbage collecting it. */
  private Duration taskRetentionDuration(Customer customer) {
    return runtimeConfigFactory
        .forCustomer(customer)
        .getDuration(YB_TASK_GC_TASK_RETENTION_DURATION);
  }
}
