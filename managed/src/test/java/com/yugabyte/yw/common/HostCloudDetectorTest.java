// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import com.yugabyte.yw.commissioner.Common.CloudType;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Predicate;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class HostCloudDetectorTest {

  @Rule public TemporaryFolder tmp = new TemporaryFolder();

  private Path dmiDir;
  private HttpServer metadataServer;
  private String metadataUrl;
  private final AtomicInteger requests = new AtomicInteger();
  // Which requests the fake metadata service answers with 200, and which with 401; others get 404.
  private Predicate<HttpExchange> answers = e -> false;
  private Predicate<HttpExchange> rejects = e -> false;

  @Before
  public void setUp() throws IOException {
    dmiDir = tmp.newFolder("dmi").toPath();
    metadataServer = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
    metadataServer.createContext(
        "/",
        exchange -> {
          requests.incrementAndGet();
          boolean ok = answers.test(exchange);
          if (ok && exchange.getRequestURI().getPath().startsWith("/computeMetadata")) {
            exchange.getResponseHeaders().add("Metadata-Flavor", "Google");
          }
          exchange.sendResponseHeaders(ok ? 200 : rejects.test(exchange) ? 401 : 404, -1);
          exchange.close();
        });
    metadataServer.start();
    metadataUrl = "http://127.0.0.1:" + metadataServer.getAddress().getPort();
  }

  @After
  public void tearDown() {
    metadataServer.stop(0);
  }

  private void dmi(String name, String value) throws IOException {
    Files.writeString(dmiDir.resolve(name), value + "\n");
  }

  private Optional<CloudType> detect() {
    return new HostCloudDetector(dmiDir, metadataUrl).getHostCloud();
  }

  @Test
  public void awsFromDmi() throws IOException {
    dmi("chassis_asset_tag", "Amazon EC2");

    assertEquals(Optional.of(CloudType.aws), detect());
    assertEquals("DMI is enough", 0, requests.get());
  }

  @Test
  public void isHostCloudMatchesOnlyTheDetectedCloud() throws IOException {
    dmi("chassis_asset_tag", "Amazon EC2");
    HostCloudDetector detector = new HostCloudDetector(dmiDir, metadataUrl);

    assertTrue(detector.isHostCloud(CloudType.aws));
    assertFalse(detector.isHostCloud(CloudType.gcp));
  }

  @Test
  public void gcpFromDmi() throws IOException {
    dmi("chassis_asset_tag", "");
    dmi("product_name", "Google Compute Engine");

    assertEquals(Optional.of(CloudType.gcp), detect());
    assertEquals(0, requests.get());
  }

  @Test
  public void azureFromDmi() throws IOException {
    dmi("chassis_asset_tag", "7783-7084-3265-9085-8269-3286-77");

    assertEquals(Optional.of(CloudType.azu), detect());
    assertEquals(0, requests.get());
  }

  @Test
  public void gcpFromMetadataService() {
    answers =
        e ->
            e.getRequestURI().getPath().startsWith("/computeMetadata/")
                && "Google".equals(e.getRequestHeaders().getFirst("Metadata-Flavor"));

    assertEquals(Optional.of(CloudType.gcp), detect());
  }

  @Test
  public void azureFromMetadataService() {
    answers =
        e ->
            e.getRequestURI().getPath().equals("/metadata/instance")
                && "true".equals(e.getRequestHeaders().getFirst("Metadata"));

    assertEquals(Optional.of(CloudType.azu), detect());
  }

  @Test
  public void awsFromImdsV2Token() {
    answers =
        e ->
            e.getRequestMethod().equals("PUT")
                && e.getRequestURI().getPath().equals("/latest/api/token");

    assertEquals(Optional.of(CloudType.aws), detect());
  }

  @Test
  public void awsFromImdsV1() {
    answers = e -> e.getRequestURI().getPath().equals("/latest/meta-data/instance-id");

    assertEquals(Optional.of(CloudType.aws), detect());
  }

  @Test
  public void awsFromImdsV1WhenImdsV2IsRequired() {
    // The IMDSv2 token response doesn't reach YBA, and IMDSv1 is disabled.
    rejects = e -> e.getRequestURI().getPath().equals("/latest/meta-data/instance-id");

    assertEquals(Optional.of(CloudType.aws), detect());
  }

  @Test
  public void noCloudWhenNothingAnswers() throws IOException {
    dmi("chassis_asset_tag", "No Asset Tag");
    dmi("product_name", "VMware Virtual Platform");

    assertEquals(Optional.empty(), detect());
  }

  @Test
  public void noCloudWhenMetadataServiceIsUnreachable() {
    metadataServer.stop(0);

    assertEquals(Optional.empty(), detect());
  }

  @Test
  public void undetectedHostIsNotProbedOnEveryCall() {
    HostCloudDetector detector = new HostCloudDetector(dmiDir, metadataUrl);

    assertEquals(Optional.empty(), detector.getHostCloud());
    int probes = requests.get();
    assertEquals(Optional.empty(), detector.getHostCloud());
    assertEquals(probes, requests.get());
  }
}
