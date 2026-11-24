// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.config;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.github.benmanes.caffeine.cache.Policy;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import io.ebean.DB;
import io.ebean.Transaction;
import java.time.Duration;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import play.cache.NamedCacheImpl;
import play.cache.SyncCacheApi;
import play.cache.caffeine.NamedCaffeineCache;
import play.inject.BindingKey;

public class RuntimeConfigCacheTest extends FakeDBApplication {

  private static final String CACHE_NAME = "runtimeConfigsCache";
  private static final String REDACTION_KEY =
      GlobalConfKeys.enableGFlagsSensitiveDataApiRedaction.getKey();

  private Customer customer;
  private RuntimeConfigCache runtimeConfigCache;
  private Policy.VarExpiration<Object, Object> expiration;
  private ExecutorService executor;

  @Before
  @SuppressWarnings("unchecked")
  public void setUp() {
    customer = ModelFactory.testCustomer();
    runtimeConfigCache = app.injector().instanceOf(RuntimeConfigCache.class);
    NamedCaffeineCache<Object, Object> caffeineCache =
        app.injector()
            .instanceOf(
                new BindingKey<>(NamedCaffeineCache.class)
                    .qualifiedWith(new NamedCacheImpl(CACHE_NAME))
                    .asScala());
    expiration = caffeineCache.synchronous().policy().expireVariably().orElseThrow();
    executor = Executors.newFixedThreadPool(2);
  }

  @After
  public void tearDown() {
    executor.shutdownNow();
  }

  private Duration expiresAfter(String key) {
    return expiration.getExpiresAfter(key).orElseThrow();
  }

  // Play loads through Caffeine's async cache. Until the load's completion step runs, just after
  // the value is returned, the entry has Caffeine's in-flight expiry instead of the one it asked
  // for.
  private Duration loadedExpiresAfter(String key) throws InterruptedException {
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
    Duration remaining = expiresAfter(key);
    while (remaining.compareTo(Duration.ofSeconds(60)) > 0 && System.nanoTime() < deadline) {
      Thread.sleep(10);
      remaining = expiresAfter(key);
    }
    return remaining;
  }

  @Test
  public void redactionKeyExpiresSixtySecondsAfterItIsLoaded() throws InterruptedException {
    runtimeConfigCache.getBoolean(REDACTION_KEY);
    Duration first = loadedExpiresAfter(REDACTION_KEY);
    assertTrue(first.compareTo(Duration.ZERO) > 0);
    assertTrue(first.compareTo(Duration.ofSeconds(60)) <= 0);

    // Reads don't extend it, so a stale value can't outlive it under a steady stream of reads.
    Thread.sleep(50);
    runtimeConfigCache.getBoolean(REDACTION_KEY);
    assertTrue(first.minus(expiresAfter(REDACTION_KEY)).compareTo(Duration.ofMillis(50)) >= 0);
  }

  @Test
  public void readBeforeTheChangeCommitsIsDroppedWhenItCommits() throws Exception {
    RuntimeConfService runtimeConfService = app.injector().instanceOf(RuntimeConfService.class);
    assertFalse(runtimeConfigCache.getBoolean(REDACTION_KEY));

    // The change listener clears the key inside the transaction that writes it, and a request on
    // another connection reads the committed value before the commit.
    try (Transaction transaction = DB.beginTransaction()) {
      runtimeConfService.setKey(customer.getUuid(), GLOBAL_SCOPE_UUID, REDACTION_KEY, "true", true);
      assertFalse(
          executor
              .submit(() -> runtimeConfigCache.getBoolean(REDACTION_KEY))
              .get(10, TimeUnit.SECONDS));
      transaction.commit();
    }
    // The commit clears the key again, so the next read gets the committed value.
    assertTrue(runtimeConfigCache.getBoolean(REDACTION_KEY));
  }

  @Test
  public void readBeforeTheChangeRollsBackIsDroppedWhenItRollsBack() throws Exception {
    RuntimeConfService runtimeConfService = app.injector().instanceOf(RuntimeConfService.class);
    assertFalse(runtimeConfigCache.getBoolean(REDACTION_KEY));

    try (Transaction transaction = DB.beginTransaction()) {
      runtimeConfService.setKey(customer.getUuid(), GLOBAL_SCOPE_UUID, REDACTION_KEY, "true", true);
      assertFalse(
          executor
              .submit(() -> runtimeConfigCache.getBoolean(REDACTION_KEY))
              .get(10, TimeUnit.SECONDS));
      assertTrue(expiration.getExpiresAfter(REDACTION_KEY).isPresent());
      transaction.rollback();
    }
    // The rollback clears the key again, so nothing loaded during the transaction stays cached.
    assertFalse(expiration.getExpiresAfter(REDACTION_KEY).isPresent());
    assertFalse(runtimeConfigCache.getBoolean(REDACTION_KEY));
  }

  @Test
  public void writeThatSkipsTheListenerIsReadWithinSixtySeconds() throws Exception {
    assertFalse(runtimeConfigCache.getBoolean(REDACTION_KEY));

    // RuntimeConfig.setValue writes the key without notifying the change listener.
    app.injector()
        .instanceOf(SettableRuntimeConfigFactory.class)
        .globalRuntimeConf()
        .setValue(REDACTION_KEY, "true");
    assertFalse(runtimeConfigCache.getBoolean(REDACTION_KEY));
    assertTrue(loadedExpiresAfter(REDACTION_KEY).compareTo(Duration.ofSeconds(60)) <= 0);

    // Once the expiry passes, the new value is read.
    expiration.setExpiresAfter(REDACTION_KEY, Duration.ofNanos(1));
    Thread.sleep(1);
    assertTrue(runtimeConfigCache.getBoolean(REDACTION_KEY));
  }

  @Test
  public void clearDuringALoadDropsTheLoadedValue() throws Exception {
    // RuntimeConfigCache loads with getOrElseUpdate and its change listener clears with remove,
    // both on this cache. A load that read the old value before a clear, such as the one after a
    // change commits, must not stay cached: Caffeine stores a load's result only if the key still
    // maps to that load when it completes.
    SyncCacheApi cache =
        app.injector()
            .instanceOf(
                new BindingKey<>(SyncCacheApi.class)
                    .qualifiedWith(new NamedCacheImpl(CACHE_NAME))
                    .asScala());
    String key = "test.in_flight_load";
    CountDownLatch loading = new CountDownLatch(1);
    CountDownLatch release = new CountDownLatch(1);
    Callable<Boolean> slowLoad =
        () -> {
          loading.countDown();
          release.await();
          return false;
        };

    Future<Boolean> read = executor.submit(() -> cache.getOrElseUpdate(key, slowLoad, 60));
    assertTrue(loading.await(10, TimeUnit.SECONDS));
    cache.remove(key);
    release.countDown();
    // The read in flight still returns what it loaded, but the next read loads again.
    assertFalse(read.get(10, TimeUnit.SECONDS));
    assertTrue(cache.getOrElseUpdate(key, () -> true, 60));
  }
}
