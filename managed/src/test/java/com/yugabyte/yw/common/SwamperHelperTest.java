// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Universe;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.Configuration;
import play.libs.Json;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class SwamperHelperTest extends FakeDBApplication {
  @Mock
  Configuration appConfig;

  @InjectMocks
  SwamperHelper swamperHelper;

  static String SWAMPER_TMP_PATH = "/tmp/swamper/";

  @BeforeClass
  public static void setUp() {
    new File(SWAMPER_TMP_PATH).mkdir();
  }

  @AfterClass
  public static void tearDown() {
    File file = new File(SWAMPER_TMP_PATH);
    file.delete();
  }

  @Test
  public void testWriteUniverseTargetJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdaterWithInactiveNodes());

    try {
      swamperHelper.writeUniverseTargetJson(u.universeUUID);
      BufferedReader br = new BufferedReader(new FileReader(SWAMPER_TMP_PATH + u.universeUUID + ".json"));
      String line;
      StringBuilder sb = new StringBuilder();
      while ((line = br.readLine()) != null) {
        sb.append(line);
      }

      JsonNode targetsJson = Json.parse(sb.toString());
      assertThat(targetsJson.size(), is(equalTo(2)));
      assertTrue(targetsJson.get(0).get("targets").isArray());
      JsonNode target1 = targetsJson.get(0);
      JsonNode target2 = targetsJson.get(1);
      assertTrue(target1.get("targets").isArray());
      assertThat(target1.get("targets").size(), equalTo(3));
      assertThat(target1.get("targets").toString(), RegexMatcher.matchesRegex("(.*)(|:)([0-9]*)"));
      assertThat(target1.get("labels").toString(), equalTo("{\"export_type\":\"node_export\",\"node_prefix\":\"host\"}"));

      assertTrue(target2.get("targets").isArray());
      assertThat(target2.get("targets").size(), equalTo(3));
      assertThat(target2.get("targets").toString(), RegexMatcher.matchesRegex("(.*)(|:)([0-9]*)"));
      assertThat(target2.get("labels").toString(), equalTo("{\"export_type\":\"collectd_export\",\"node_prefix\":\"host\"}"));
    } catch (Exception e) {
      assertNotNull(e.getMessage());
    }
  }

  @Test
  public void testRemoveUniverseTargetJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID universeUUID = UUID.randomUUID();
    try {
      new File(SWAMPER_TMP_PATH + universeUUID + ".json").createNewFile();
    } catch (IOException e) {
      assertNotNull(e.getMessage());
    }
    swamperHelper.removeUniverseTargetJson(universeUUID);
    assertFalse(new File(SWAMPER_TMP_PATH + universeUUID + ".json").exists());
  }

  @Test(expected = RuntimeException.class)
  public void testUniverseTargetWriteFailure() {
    File dir = new File("/tmp/non-writable");
    dir.mkdir();
    dir.setWritable(false);

    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(dir.getPath());
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    swamperHelper.writeUniverseTargetJson(u.universeUUID);
  }

  public void testUniverseTargetWithoutTargetPath() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn("");
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    swamperHelper.writeUniverseTargetJson(u.universeUUID);
  }
}
