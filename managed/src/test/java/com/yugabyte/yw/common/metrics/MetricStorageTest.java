package com.yugabyte.yw.common.metrics;

import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.filters.MetricFilter;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import org.junit.Test;

public class MetricStorageTest {
  private final MetricStorage metricStorage = new MetricStorage();

  @Test
  public void testConcurrentDeleteAndSaveWithSameName()
      throws InterruptedException, ExecutionException {
    CountDownLatch saveBarrier = new CountDownLatch(1);
    UUID toDeleteCustomerUUID = UUID.randomUUID();
    List<Metric> toDelete =
        IntStream.range(0, 50)
            .mapToObj(
                i ->
                    new Metric()
                        .setName("metric")
                        .setValue(0d)
                        .setCustomerUUID(toDeleteCustomerUUID))
            .collect(Collectors.toList());
    List<Metric> metrics =
        IntStream.range(0, 5_000)
            .mapToObj(
                i -> new Metric().setName("metric").setValue(0d).setCustomerUUID(UUID.randomUUID()))
            .collect(Collectors.toList());

    ExecutorService saveThread = Executors.newFixedThreadPool(1);
    Future<?> future =
        saveThread.submit(
            () -> {
              metricStorage.save(toDelete);
              saveBarrier.countDown();
              metricStorage.save(metrics);
            });

    saveBarrier.await();

    metricStorage.delete(MetricFilter.builder().customerUuid(toDeleteCustomerUUID).build());

    saveThread.shutdown();
    future.get();
  }

  @Test
  public void testConcurrentDeleteAndSaveWithSameCustomer()
      throws InterruptedException, ExecutionException {
    CountDownLatch saveBarrier = new CountDownLatch(1);
    UUID customerUUID = UUID.randomUUID();
    UUID toDeleteSourceUUID = UUID.randomUUID();
    List<Metric> toDelete =
        IntStream.range(0, 50)
            .mapToObj(
                i ->
                    new Metric()
                        .setName("metric")
                        .setValue(0d)
                        .setCustomerUUID(customerUUID)
                        .setSourceUuid(toDeleteSourceUUID))
            .collect(Collectors.toList());
    List<Metric> metrics =
        IntStream.range(0, 5_000)
            .mapToObj(
                i ->
                    new Metric()
                        .setName("metric")
                        .setValue(0d)
                        .setCustomerUUID(customerUUID)
                        .setSourceUuid(UUID.randomUUID()))
            .collect(Collectors.toList());

    ExecutorService saveThread = Executors.newFixedThreadPool(1);
    Future<?> future =
        saveThread.submit(
            () -> {
              metricStorage.save(toDelete);
              saveBarrier.countDown();
              metricStorage.save(metrics);
            });

    saveBarrier.await();

    metricStorage.delete(MetricFilter.builder().sourceUuid(toDeleteSourceUUID).build());

    saveThread.shutdown();
    future.get();
  }

  @Test
  public void testConcurrentReadsAndUpdatesForSameSourceAndCustomer()
      throws ExecutionException, InterruptedException {
    UUID customerUUID = UUID.randomUUID();
    UUID sourceUUID = UUID.randomUUID();
    List<Metric> metrics =
        IntStream.range(0, 5_000)
            .mapToObj(
                i ->
                    new Metric()
                        .setName("metric")
                        .setValue(0d)
                        .setCustomerUUID(customerUUID)
                        .setSourceUuid(sourceUUID)
                        .setKeyLabel("label" + i, "value" + i))
            .collect(Collectors.toList());

    ExecutorService executorService = Executors.newFixedThreadPool(2);
    Future<?> updateFuture = executorService.submit(() -> metricStorage.save(metrics));
    Future<?> readFuture =
        executorService.submit(
            () -> {
              for (Metric m : metrics) {
                metricStorage.get(MetricKey.from(m));
              }
            });

    updateFuture.get();
    readFuture.get();
    executorService.shutdown();
  }
}
