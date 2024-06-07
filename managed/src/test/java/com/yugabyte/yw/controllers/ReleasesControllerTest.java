package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Users;
import java.util.Iterator;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class ReleasesControllerTest extends FakeDBApplication {
  private ConfigHelper configHelper;

  Customer defaultCustomer;
  Users defaultUser;

  @Before
  public void setUp() {
    this.defaultCustomer = ModelFactory.testCustomer();
    this.defaultUser = ModelFactory.testUser(defaultCustomer);

    configHelper = app.injector().instanceOf(ConfigHelper.class);
    configHelper.loadConfigToDB(
        ConfigType.SoftwareVersion, ImmutableMap.of("version", "2.23.0.0-b1"));
  }

  @Test
  public void testCreateRelease() {
    String url = String.format("/api/customers/%s/ybdb_release", defaultCustomer.getUuid());
    ObjectNode jsonBody = Json.newObject();
    UUID r1UUID = UUID.randomUUID();
    jsonBody.put("release_uuid", r1UUID.toString());
    jsonBody.put("version", "2.21.1.0");
    jsonBody.put("yb_type", "YBDB");
    jsonBody.put("release_type", "LTS");
    ArrayNode jArtifacts = Json.newArray();
    ObjectNode jArtifact = Json.newObject();
    jArtifact.put("package_url", "http://download.yugabyte.com/my_release.tgz");
    jArtifact.put("platform", ReleaseArtifact.Platform.LINUX.toString());
    jArtifact.put("architecture", Architecture.x86_64.toString());
    jArtifact.put("sha256", "1234asdf");
    jArtifacts.add(jArtifact);
    jsonBody.set("artifacts", jArtifacts);
    Result result = doPostRequest(url, jsonBody);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertTrue(r1UUID.equals(UUID.fromString(resultJson.get("resourceUUID").asText())));

    Release foundRelease = Release.get(r1UUID);
    assertNotNull(foundRelease);
    assertEquals("2.21.1.0", foundRelease.getVersion());
    assertEquals("YBDB", foundRelease.getYb_type().toString());
    assertEquals("LTS", foundRelease.getReleaseType());
    List<ReleaseArtifact> artifacts = foundRelease.getArtifacts();
    assertEquals(1, artifacts.size());
    assertEquals("http://download.yugabyte.com/my_release.tgz", artifacts.get(0).getPackageURL());
    assertEquals("1234asdf", artifacts.get(0).getSha256());
    assertEquals("sha256:1234asdf", artifacts.get(0).getFormattedSha256());
    assertEquals(ReleaseArtifact.Platform.LINUX, artifacts.get(0).getPlatform());
    assertEquals(Architecture.x86_64, artifacts.get(0).getArchitecture());
  }

  @Test
  public void testCreateReleaseKubernetes() {
    String url = String.format("/api/customers/%s/ybdb_release", defaultCustomer.getUuid());
    ObjectNode jsonBody = Json.newObject();
    UUID r1UUID = UUID.randomUUID();
    jsonBody.put("release_uuid", r1UUID.toString());
    jsonBody.put("version", "2.21.1.0");
    jsonBody.put("yb_type", "YBDB");
    jsonBody.put("release_type", "LTS");
    ArrayNode jArtifacts = Json.newArray();
    ObjectNode jArtifact = Json.newObject();
    jArtifact.put("package_url", "http://download.yugabyte.com/my_release.tgz");
    jArtifact.put("platform", ReleaseArtifact.Platform.KUBERNETES.toString());
    jArtifact.put("sha256", "1234asdf");
    jArtifacts.add(jArtifact);
    jsonBody.set("artifacts", jArtifacts);
    Result result = doPostRequest(url, jsonBody);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertTrue(r1UUID.equals(UUID.fromString(resultJson.get("resourceUUID").asText())));

    Release foundRelease = Release.get(r1UUID);
    assertNotNull(foundRelease);
    assertEquals("2.21.1.0", foundRelease.getVersion());
    assertEquals("YBDB", foundRelease.getYb_type().toString());
    assertEquals("LTS", foundRelease.getReleaseType());
    List<ReleaseArtifact> artifacts = foundRelease.getArtifacts();
    assertEquals(1, artifacts.size());
    assertEquals("http://download.yugabyte.com/my_release.tgz", artifacts.get(0).getPackageURL());
    assertEquals("1234asdf", artifacts.get(0).getSha256());
    assertEquals("sha256:1234asdf", artifacts.get(0).getFormattedSha256());
    assertEquals(ReleaseArtifact.Platform.KUBERNETES, artifacts.get(0).getPlatform());
    assertNull(artifacts.get(0).getArchitecture());
  }

  @Test
  public void testListRelease() {
    Release r1 = Release.create("2.21.0.1", "STS");
    Release r2 = Release.create("2.22.0.2", "LTS");
    String url = String.format("/api/customers/%s/ybdb_release", defaultCustomer.getUuid());
    Result result = doGetRequest(url);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    boolean foundR1 = false;
    boolean foundR2 = false;
    Iterator<JsonNode> it = resultJson.elements();
    while (it.hasNext()) {
      JsonNode next = it.next();
      String version = next.get("version").asText();
      if (version.equals("2.21.0.1") && !foundR1) {
        foundR1 = true;
        assertEquals(r1.getReleaseType().toString(), next.get("release_type").asText());
      } else if (version.equals("2.22.0.2") && !foundR2) {
        foundR2 = true;
        assertEquals(r2.getReleaseType().toString(), next.get("release_type").asText());
      } else {
        throw new AssertionError("unexpected release " + version);
      }
    }
    assertTrue(foundR1);
    assertTrue(foundR2);
  }

  @Test
  public void testListX86Releases() {
    Release r1 = Release.create("2.21.0.1", "STS");
    Release r2 = Release.create("2.22.0.2", "LTS");
    ReleaseArtifact art1 =
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, "file_url");
    ReleaseArtifact art2 =
        ReleaseArtifact.create(
            "sha257", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "file_urls");
    r1.addArtifact(art1);
    r2.addArtifact(art2);
    String url =
        String.format(
            "/api/customers/%s/ybdb_release?deployment_type=x86_64", defaultCustomer.getUuid());
    Result result = doGetRequest(url);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    Iterator<JsonNode> it = resultJson.elements();
    while (it.hasNext()) {
      JsonNode next = it.next();
      String version = next.get("version").asText();
      if (version.equals("2.22.0.2")) {
        assertEquals(r2.getReleaseType().toString(), next.get("release_type").asText());
      } else {
        throw new AssertionError("unexpected release " + version);
      }
    }
  }

  @Test
  public void testListKubernetesReleases() {
    Release r1 = Release.create("2.21.0.1", "STS");
    Release r2 = Release.create("2.22.0.2", "LTS");
    ReleaseArtifact art1 =
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, "file_url");
    ReleaseArtifact art2 =
        ReleaseArtifact.create(
            "sha257", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "file_urls");
    r1.addArtifact(art1);
    r2.addArtifact(art2);
    String url =
        String.format(
            "/api/customers/%s/ybdb_release?deployment_type=kubernetes", defaultCustomer.getUuid());
    Result result = doGetRequest(url);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    Iterator<JsonNode> it = resultJson.elements();
    while (it.hasNext()) {
      JsonNode next = it.next();
      String version = next.get("version").asText();
      if (version.equals("2.21.0.1")) {
        assertEquals(r1.getReleaseType().toString(), next.get("release_type").asText());
      } else {
        throw new AssertionError("unexpected release " + version);
      }
    }
  }

  @Test
  public void testGetRelease() {
    Release r1 = Release.create("2.21.0.1", "STS");
    Release.create("2.22.0.2", "LTS");
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    Result result = doGetRequest(url);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertEquals(r1.getVersion(), resultJson.get("version").asText());
    assertEquals(r1.getReleaseUUID().toString(), resultJson.get("release_uuid").asText());
  }

  @Test
  public void testGetReleaseWithArtifacts() {
    Release r1 = Release.create("2.21.0.1", "STS");
    ReleaseArtifact linuxArtifact =
        ReleaseArtifact.create(
            "sha256-linux",
            ReleaseArtifact.Platform.LINUX,
            Architecture.x86_64,
            "http://linux.com");
    ReleaseArtifact k8sArtifact =
        ReleaseArtifact.create(
            "sha256-k8s", ReleaseArtifact.Platform.KUBERNETES, null, "http://k8s.com");
    r1.addArtifact(linuxArtifact);
    r1.addArtifact(k8sArtifact);
    Release.create("2.22.0.2", "LTS");
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    Result result = doGetRequest(url);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertEquals(r1.getVersion(), resultJson.get("version").asText());
    assertEquals(r1.getReleaseUUID().toString(), resultJson.get("release_uuid").asText());
    JsonNode artifacts = resultJson.get("artifacts");
    assertTrue(artifacts.isArray());
    Integer count = 0;
    boolean foundLinux = false;
    boolean foundK8s = false;
    for (JsonNode artifact : artifacts) {
      count++;
      if (artifact.get("package_url").asText().equals("http://linux.com") && !foundLinux) {
        foundLinux = true;
        assertEquals(ReleaseArtifact.Platform.LINUX.toString(), artifact.get("platform").asText());
        assertEquals(Architecture.x86_64.toString(), artifact.get("architecture").asText());
      } else if (artifact.get("package_url").asText().equals("http://k8s.com") && !foundK8s) {
        foundK8s = true;
        assertEquals(
            ReleaseArtifact.Platform.KUBERNETES.toString(), artifact.get("platform").asText());
        assertTrue(artifact.get("architecture").isNull());
      }
    }
    assertTrue(2 == count);
    assertTrue(foundK8s);
    assertTrue(foundLinux);
  }

  @Test
  public void testUpdateReleaseState() {
    Release r1 = Release.create("2.21.0.1", "STS");
    r1.addArtifact(
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "url"));
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    ObjectNode jsonBody = Json.newObject();
    jsonBody.put("state", "DISABLED");
    Result result = doPutRequest(url, jsonBody);
    assertOk(result);
    Release found = Release.get(r1.getReleaseUUID());
    assertEquals(Release.ReleaseState.DISABLED, found.getState());
  }

  @Test
  public void testIncompleteStateUpdate() {
    Release r1 = Release.create("2.21.0.1", "STS");
    r1.addArtifact(
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, "url"));
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    ObjectNode jsonBody = Json.newObject();
    jsonBody.put("state", "ACTIVE");
    Result result = assertPlatformException(() -> doPutRequest(url, jsonBody));
    assertBadRequest(result, null);
    Release found = Release.get(r1.getReleaseUUID());
    assertEquals(Release.ReleaseState.INCOMPLETE, found.getState());
  }

  @Test
  public void testUpdateReleaseExistingArtifact() {
    Release r1 = Release.create("2.21.0.1", "STS");
    ReleaseArtifact ra1 =
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "http://url.com");
    r1.addArtifact(ra1);
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    ObjectNode jsonBody = Json.newObject();
    ArrayNode jArtifacts = Json.newArray();
    ObjectNode jArtifact = Json.newObject();
    jArtifact.put("package_url", "http://url.com");
    jArtifact.put("platform", ReleaseArtifact.Platform.LINUX.toString());
    jArtifact.put("architecture", Architecture.x86_64.toString());
    jArtifact.put("sha256", "new_sha256");
    jArtifacts.add(jArtifact);
    jsonBody.set("artifacts", jArtifacts);
    Result result = doPutRequest(url, jsonBody);
    assertOk(result);
    ReleaseArtifact foundRA = ReleaseArtifact.get(ra1.getArtifactUUID());
    assertEquals("new_sha256", foundRA.getSha256());
    assertEquals("sha256:new_sha256", foundRA.getFormattedSha256());
  }

  @Test
  public void testUpdateReleaseNewArtifact() {
    Release r1 = Release.create("2.21.0.1", "STS");
    ReleaseArtifact ra1 =
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "http://url.com");
    r1.addArtifact(ra1);
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    final ObjectNode jsonBody = Json.newObject();
    ArrayNode jArtifacts = Json.newArray();
    ObjectNode jArtifact = Json.newObject();
    jArtifact.put("package_url", "http://url.com");
    jArtifact.put("platform", ReleaseArtifact.Platform.LINUX.toString());
    jArtifact.put("architecture", Architecture.x86_64.toString());
    jArtifacts.add(jArtifact);
    ObjectNode jArtifact2 = Json.newObject();
    jArtifact2.put("package_url", "http://other.com");
    jArtifact2.put("platform", ReleaseArtifact.Platform.LINUX.toString());
    jArtifact2.put("architecture", Architecture.x86_64.toString());
    jArtifact2.put("sha256", "new_sha256");
    jArtifacts.add(jArtifact2);
    jsonBody.set("artifacts", jArtifacts);
    System.out.println("testing bad request");
    Result result = assertPlatformException(() -> doPutRequest(url, jsonBody));
    assertBadRequest(
        result, "artifact matching platform LINUX and architecture x86_64 already exists");
    final ObjectNode jsonBody2 = Json.newObject();
    jArtifacts = Json.newArray();
    jArtifact = Json.newObject();
    jArtifact.put("package_url", "http://url.com");
    jArtifact.put("platform", ReleaseArtifact.Platform.LINUX.toString());
    jArtifact.put("architecture", Architecture.x86_64.toString());
    jArtifacts.add(jArtifact);
    jArtifact2 = Json.newObject();
    jArtifact2.put("package_url", "http://other.com");
    jArtifact2.put("platform", ReleaseArtifact.Platform.LINUX.toString());
    jArtifact2.put("architecture", Architecture.aarch64.toString());
    jArtifact2.put("sha256", "new_sha256");
    jArtifacts.add(jArtifact2);
    jsonBody2.set("artifacts", jArtifacts);
    System.out.println("testing good request");
    result = doPutRequest(url, jsonBody2);
    assertOk(result);
    List<ReleaseArtifact> artifacts = r1.getArtifacts();
    assertEquals(2, artifacts.size());
    boolean foundOriginal = false;
    boolean foundNew = false;
    for (ReleaseArtifact artifact : artifacts) {
      if (artifact.getArtifactUUID().equals(ra1.getArtifactUUID())) {
        foundOriginal = true;
      } else {
        foundNew = true;
        assertEquals("http://other.com", artifact.getPackageURL());
      }
    }
    assertTrue(foundOriginal);
    assertTrue(foundNew);
  }

  @Test
  public void testUpdateReleaseDeleteArtifact() {
    Release r1 = Release.create("2.21.0.1", "STS");
    ReleaseArtifact ra1 =
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "http://url.com");
    r1.addArtifact(ra1);
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    ObjectNode jsonBody = Json.newObject();
    ArrayNode jArtifacts = Json.newArray();
    jsonBody.set("artifacts", jArtifacts);
    Result result = doPutRequest(url, jsonBody);
    assertOk(result);
    List<ReleaseArtifact> artifacts = r1.getArtifacts();
    assertEquals(0, artifacts.size());
  }

  @Test
  public void testDeleteRelease() {
    Release r1 = Release.create("2.21.0.1", "STS");
    ReleaseArtifact ra1 =
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "http://url.com");
    r1.addArtifact(ra1);
    String url =
        String.format(
            "/api/customers/%s/ybdb_release/%s",
            defaultCustomer.getUuid(), r1.getReleaseUUID().toString());
    Result result = doDeleteRequest(url);
    assertOk(result);
    assertNull(Release.get(r1.getReleaseUUID()));
    assertNull(ReleaseArtifact.get(ra1.getArtifactUUID()));
  }

  private Result doPostRequest(String url, ObjectNode jsonBody) {
    String authToken = defaultUser.createAuthToken();
    return doRequestWithAuthTokenAndBody("POST", url, authToken, jsonBody);
  }

  private Result doGetRequest(String url) {
    String authToken = defaultUser.createAuthToken();
    return doRequestWithAuthToken("GET", url, authToken);
  }

  private Result doPutRequest(String url, ObjectNode jsonBody) {
    String authToken = defaultUser.createAuthToken();
    return doRequestWithAuthTokenAndBody("PUT", url, authToken, jsonBody);
  }

  private Result doDeleteRequest(String url) {
    String authToken = defaultUser.createAuthToken();
    return doRequestWithAuthToken("DELETE", url, authToken);
  }
}
