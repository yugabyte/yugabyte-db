// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.CloudMetadataEndpoints.AWS_IMDSV2_TOKEN_PATH;
import static com.yugabyte.yw.common.CloudMetadataEndpoints.AWS_IMDSV2_TOKEN_TTL_HEADER;
import static com.yugabyte.yw.common.CloudMetadataEndpoints.AWS_INSTANCE_ID_PATH;
import static com.yugabyte.yw.common.CloudMetadataEndpoints.GCP_INSTANCE_ID_PATH;
import static com.yugabyte.yw.common.CloudMetadataEndpoints.GCP_METADATA_FLAVOR;
import static com.yugabyte.yw.common.CloudMetadataEndpoints.GCP_METADATA_FLAVOR_HEADER;
import static com.yugabyte.yw.common.CloudMetadataEndpoints.METADATA_IP;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common.CloudType;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.function.Predicate;
import lombok.extern.slf4j.Slf4j;

/**
 * Detects the public cloud that YBA runs on, so that only that cloud's metadata service is queried.
 * Reads DMI first, as Aeon's boot scripts do; if DMI is missing or unrecognized, probes the three
 * metadata services in parallel.
 */
@Singleton
@Slf4j
public class HostCloudDetector {
  private static final Path DMI_DIR = Paths.get("/sys/devices/virtual/dmi/id");
  private static final String METADATA_URL = "http://" + METADATA_IP;
  private static final String AZURE_CHASSIS_ASSET_TAG = "7783-7084-3265-9085-8269-3286-77";
  private static final Duration PROBE_TIMEOUT = Duration.ofSeconds(1);
  // An undetected host is probed again after this long, in case the metadata service was only
  // briefly unreachable. A detected cloud doesn't change.
  private static final Duration UNDETECTED_TTL = Duration.ofMinutes(5);

  private final Path dmiDir;
  private final String metadataUrl;
  private final HttpClient httpClient;

  private CloudType detected;
  private long undetectedUntilNanos;
  private boolean checked;

  public HostCloudDetector() {
    this(DMI_DIR, METADATA_URL);
  }

  @VisibleForTesting
  HostCloudDetector(Path dmiDir, String metadataUrl) {
    this.dmiDir = dmiDir;
    this.metadataUrl = metadataUrl;
    this.httpClient =
        HttpClient.newBuilder()
            // YBA's default ProxySelector sends hosts not in no_proxy through http_proxy.
            .proxy(HttpClient.Builder.NO_PROXY)
            .connectTimeout(PROBE_TIMEOUT)
            .followRedirects(HttpClient.Redirect.NEVER)
            .build();
  }

  public synchronized Optional<CloudType> getHostCloud() {
    if (detected == null && (!checked || System.nanoTime() - undetectedUntilNanos > 0)) {
      detected = fromDmi().or(this::fromMetadataServices).orElse(null);
      checked = true;
      undetectedUntilNanos = System.nanoTime() + UNDETECTED_TTL.toNanos();
      log.info("YBA host cloud: {}", detected == null ? "none detected" : detected);
    }
    return Optional.ofNullable(detected);
  }

  /** Whether YBA was detected running on {@code cloud}; false when no cloud was detected. */
  public boolean isHostCloud(CloudType cloud) {
    return getHostCloud().equals(Optional.of(cloud));
  }

  private Optional<CloudType> fromDmi() {
    String chassisAssetTag = readDmi("chassis_asset_tag");
    if ("Amazon EC2".equals(chassisAssetTag)) {
      return Optional.of(CloudType.aws);
    }
    if (AZURE_CHASSIS_ASSET_TAG.equals(chassisAssetTag)) {
      return Optional.of(CloudType.azu);
    }
    if ("Google Compute Engine".equals(readDmi("product_name"))) {
      return Optional.of(CloudType.gcp);
    }
    return Optional.empty();
  }

  private String readDmi(String name) {
    try {
      return Files.readString(dmiDir.resolve(name), StandardCharsets.UTF_8).trim();
    } catch (IOException | SecurityException e) {
      return null;
    }
  }

  private Optional<CloudType> fromMetadataServices() {
    // GCP and Azure are recognized by their responses. AWS is checked last: the IMDSv1 paths are
    // also served by EC2-compatible clouds. The IMDSv1 path covers instances whose IMDSv2 token
    // response can't reach YBA, e.g. a container behind a hop limit of 1: it answers 200 if IMDSv1
    // is enabled, and 401 if IMDSv2 is required.
    CompletableFuture<Boolean> gcp =
        probe(
            get(GCP_INSTANCE_ID_PATH).header(GCP_METADATA_FLAVOR_HEADER, GCP_METADATA_FLAVOR),
            r ->
                r.statusCode() == 200
                    && r.headers()
                        .allValues(GCP_METADATA_FLAVOR_HEADER)
                        .contains(GCP_METADATA_FLAVOR));
    CompletableFuture<Boolean> azu =
        probe(
            get("/metadata/instance?api-version=2021-02-01").header("Metadata", "true"),
            r -> r.statusCode() == 200);
    CompletableFuture<Boolean> awsV2 =
        probe(
            request(AWS_IMDSV2_TOKEN_PATH)
                .header(AWS_IMDSV2_TOKEN_TTL_HEADER, "60")
                .PUT(HttpRequest.BodyPublishers.noBody()),
            r -> r.statusCode() == 200);
    CompletableFuture<Boolean> awsV1 =
        probe(get(AWS_INSTANCE_ID_PATH), r -> r.statusCode() == 200 || r.statusCode() == 401);
    if (gcp.join()) {
      return Optional.of(CloudType.gcp);
    }
    if (azu.join()) {
      return Optional.of(CloudType.azu);
    }
    if (awsV2.join() || awsV1.join()) {
      return Optional.of(CloudType.aws);
    }
    return Optional.empty();
  }

  private HttpRequest.Builder request(String path) {
    return HttpRequest.newBuilder(URI.create(metadataUrl + path)).timeout(PROBE_TIMEOUT);
  }

  private HttpRequest.Builder get(String path) {
    return request(path).GET();
  }

  private CompletableFuture<Boolean> probe(
      HttpRequest.Builder request, Predicate<HttpResponse<Void>> matches) {
    return httpClient
        .sendAsync(request.build(), HttpResponse.BodyHandlers.discarding())
        .thenApply(matches::test)
        .exceptionally(e -> false);
  }
}
