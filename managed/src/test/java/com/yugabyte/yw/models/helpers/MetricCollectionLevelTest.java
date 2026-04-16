package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.Environment;

@RunWith(MockitoJUnitRunner.class)
public class MetricCollectionLevelTest {

  @Mock private Environment environment;

  @Test
  public void testGetPriorityRegex() {
    // Mock the environment to return a test params file
    String testParamsContent =
        "{\"priority_regex\": [\"test1\", \"test2\"], \"show_help\": \"false\"}";
    InputStream inputStream =
        new ByteArrayInputStream(testParamsContent.getBytes(StandardCharsets.UTF_8));
    when(environment.resourceAsStream(anyString())).thenReturn(inputStream);

    // Use the NORMAL level which has a params file
    MetricCollectionLevel level = MetricCollectionLevel.NORMAL;

    String result = level.getPriorityRegex(environment);
    assertEquals("test1test2", result);
  }

  @Test
  public void testGetMetrics() {
    // Mock the environment to return a test params file
    String testParamsContent =
        "{\"metrics\": [\"metric1\", \"metric2\"], \"priority_regex\": [\"test\"]}";
    InputStream inputStream =
        new ByteArrayInputStream(testParamsContent.getBytes(StandardCharsets.UTF_8));
    when(environment.resourceAsStream(anyString())).thenReturn(inputStream);

    // Use the MINIMAL level which has a params file
    MetricCollectionLevel level = MetricCollectionLevel.MINIMAL;

    String result = level.getMetrics(environment);
    assertEquals("metric1metric2", result);
  }

  @Test
  public void testParseParamsFile() {
    // Mock the environment to return a test params file
    String testParamsContent =
        "{\"priority_regex\": [\"test1\", \"test2\"], \"show_help\": \"false\", \"metrics\":"
            + " [\"metric1\"]}";
    InputStream inputStream =
        new ByteArrayInputStream(testParamsContent.getBytes(StandardCharsets.UTF_8));
    when(environment.resourceAsStream(anyString())).thenReturn(inputStream);

    // Use the NORMAL level which has a params file
    MetricCollectionLevel level = MetricCollectionLevel.NORMAL;

    var result = level.parseParamsFile(environment);
    assertEquals(3, result.size());
    assertEquals("test1test2", result.get("priority_regex"));
    assertEquals("false", result.get("show_help"));
    assertEquals("metric1", result.get("metrics"));
  }

  @Test
  public void testEmptyParamsFile() {
    // Use the ALL level which has no params file
    MetricCollectionLevel level = MetricCollectionLevel.ALL;

    var result = level.parseParamsFile(environment);
    assertTrue(result.isEmpty());
  }
}
