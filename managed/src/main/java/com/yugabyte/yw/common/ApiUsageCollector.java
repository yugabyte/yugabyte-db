// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.time.Instant;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import javax.annotation.Nullable;
import javax.inject.Singleton;
import play.libs.Json;

/**
 * In-memory API usage counters reported through callhome. Counts are kept per client and API
 * version, plus per route for deprecated APIs only, so the payload stays small.
 *
 * <p>Counts are not persisted: a restart or HA failover drops the unreported window, which shows up
 * as a later window_start in the next report.
 */
@Singleton
public class ApiUsageCollector {
  // Caps the distinct keys per map so a client sending random User-Agents can't grow the payload.
  static final int MAX_ENTRIES = 200;

  public record ClientKey(
      String apiVersion, String client, String clientVersion, String authType) {}

  public record DeprecatedApiKey(String apiVersion, String method, String route, String client) {}

  public record Counts(long calls, long clientErrors, long serverErrors) {
    static Counts of(int status) {
      return new Counts(1, status >= 400 && status < 500 ? 1 : 0, status >= 500 ? 1 : 0);
    }

    Counts plus(Counts other) {
      return new Counts(
          calls + other.calls,
          clientErrors + other.clientErrors,
          serverErrors + other.serverErrors);
    }

    Counts minus(Counts other) {
      return new Counts(
          calls - other.calls,
          clientErrors - other.clientErrors,
          serverErrors - other.serverErrors);
    }

    boolean isZero() {
      return calls == 0 && clientErrors == 0 && serverErrors == 0;
    }
  }

  public record Snapshot(
      Instant windowStart,
      Instant windowEnd,
      Map<ClientKey, Counts> clients,
      Map<DeprecatedApiKey, Counts> deprecatedApis,
      long droppedClientCalls,
      long droppedDeprecatedApiCalls) {

    public ObjectNode toJson() {
      ObjectNode json =
          Json.newObject()
              .put("window_start", windowStart.getEpochSecond())
              .put("window_end", windowEnd.getEpochSecond())
              .put("dropped_client_calls", droppedClientCalls)
              .put("dropped_deprecated_api_calls", droppedDeprecatedApiCalls);
      ArrayNode clientsJson = json.putArray("clients");
      clients.forEach(
          (k, c) ->
              putCounts(
                  clientsJson
                      .addObject()
                      .put("api_version", k.apiVersion())
                      .put("client", k.client())
                      .put("client_version", k.clientVersion())
                      .put("auth_type", k.authType()),
                  c));
      ArrayNode deprecatedJson = json.putArray("deprecated_apis");
      deprecatedApis.forEach(
          (k, c) ->
              putCounts(
                  deprecatedJson
                      .addObject()
                      .put("api_version", k.apiVersion())
                      .put("method", k.method())
                      .put("route", k.route())
                      .put("client", k.client()),
                  c));
      return json;
    }

    private static void putCounts(ObjectNode node, Counts c) {
      node.put("calls", c.calls()).put("errors_4xx", c.clientErrors());
      node.put("errors_5xx", c.serverErrors());
    }
  }

  private final Map<ClientKey, Counts> clients = new ConcurrentHashMap<>();
  private final Map<DeprecatedApiKey, Counts> deprecatedApis = new ConcurrentHashMap<>();
  // Calls left out of the counts above because their key was over MAX_ENTRIES.
  private final AtomicLong droppedClientCalls = new AtomicLong();
  private final AtomicLong droppedDeprecatedApiCalls = new AtomicLong();
  private volatile Instant windowStart = Instant.now();

  public void record(ClientKey clientKey, @Nullable DeprecatedApiKey deprecatedKey, int status) {
    Counts counts = Counts.of(status);
    add(clients, clientKey, counts, droppedClientCalls);
    if (deprecatedKey != null) {
      add(deprecatedApis, deprecatedKey, counts, droppedDeprecatedApiCalls);
    }
  }

  private <K> void add(Map<K, Counts> map, K key, Counts counts, AtomicLong droppedCalls) {
    // The size check races with concurrent inserts; overshooting the cap by a few is fine.
    if (map.size() >= MAX_ENTRIES && !map.containsKey(key)) {
      droppedCalls.incrementAndGet();
      return;
    }
    map.merge(key, counts, Counts::plus);
  }

  /** Copies the current counters without resetting them; see {@link #acknowledge(Snapshot)}. */
  public Snapshot snapshot(Instant now) {
    return new Snapshot(
        windowStart,
        now,
        Map.copyOf(clients),
        Map.copyOf(deprecatedApis),
        droppedClientCalls.get(),
        droppedDeprecatedApiCalls.get());
  }

  /**
   * Subtracts a snapshot once it was delivered, keeping calls recorded after the snapshot was
   * taken. An unacknowledged snapshot is simply included again in the next report.
   */
  public void acknowledge(Snapshot snapshot) {
    snapshot.clients().forEach((k, c) -> subtract(clients, k, c));
    snapshot.deprecatedApis().forEach((k, c) -> subtract(deprecatedApis, k, c));
    droppedClientCalls.addAndGet(-snapshot.droppedClientCalls());
    droppedDeprecatedApiCalls.addAndGet(-snapshot.droppedDeprecatedApiCalls());
    windowStart = snapshot.windowEnd();
  }

  private <K> void subtract(Map<K, Counts> map, K key, Counts counts) {
    map.computeIfPresent(
        key,
        (k, current) -> {
          Counts left = current.minus(counts);
          return left.isZero() ? null : left;
        });
  }
}
