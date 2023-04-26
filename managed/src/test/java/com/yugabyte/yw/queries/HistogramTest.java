package com.yugabyte.yw.queries;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.node.ArrayNode;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.Test;
import play.libs.Json;

public class HistogramTest {

  @Test
  public void testCreate() throws IOException {
    String json =
        "[{\"[0.2,0.3)\": 3}, {\"[0.8,0.9)\": 2}, {\"[1677721.6,)\": 1}, {\"[0.1,0.2)\": 4}]";
    List<Map<String, Integer>> mapList = new ArrayList<>();
    mapList.addAll(
        Json.mapper().readValue(json, new TypeReference<List<HashMap<String, Integer>>>() {}));
    Histogram htg = new Histogram(mapList);
    // Ensure correct ordering
    String firstKey = htg.getBins().firstKey(), lastKey = htg.getBins().lastKey();
    Double d1 = Double.parseDouble(firstKey.substring(1, firstKey.indexOf(",")));
    Double d2 = Double.parseDouble(lastKey.substring(1, lastKey.indexOf(",")));
    assertTrue(d1.equals(0.1));
    assertTrue(d2.equals(1677721.6));

    assertEquals(htg.getTotalCount(), 10);
  }

  @Test
  public void testMerge() throws IOException {
    String json1 =
        "[{\"[0.1,0.2)\": 4}, {\"[0.2,0.3)\": 1}, {\"[0.8,0.9)\": 1}, {\"[1677721.6,)\": 5}]";
    String json2 =
        "[{\"[0.1,0.2)\": 2}, {\"[0.3,0.4)\": 1}, {\"[0.5,0.6)\": 1}, {\"[1677721.6,)\": 5}]";
    ArrayNode arrayNode1 = (ArrayNode) Json.mapper().readTree(json1);
    List<Map<String, Integer>> mapList1 = new ArrayList<>(), mapList2 = new ArrayList<>();
    mapList1.addAll(
        Json.mapper().readValue(json1, new TypeReference<List<HashMap<String, Integer>>>() {}));
    mapList2.addAll(
        Json.mapper().readValue(json2, new TypeReference<List<HashMap<String, Integer>>>() {}));
    Histogram htg1 = new Histogram(mapList1);
    Histogram htg2 = new Histogram(mapList2);

    // test correct ArrayNode generation
    assertTrue(arrayNode1.equals(htg1.getArrayNode()));

    htg1.merge(htg2);
    assertEquals(htg1.getTotalCount(), 20);
    assertEquals(htg1.getBins().firstEntry().getValue(), Integer.valueOf(6));
    assertEquals(htg1.getBins().lastEntry().getValue(), Integer.valueOf(10));
    assertEquals(htg1.getBins().keySet().size(), 6);
  }

  @Test
  public void testPercentile() throws IOException {
    String json =
        "[{\"[0.1,0.2)\": 5}, {\"[0.2,0.3)\": 5}, {\"[0.8,0.9)\": 5}, {\"[1677721.6,)\": 5}]";
    List<Map<String, Integer>> list = new ArrayList<>();
    list.addAll(
        Json.mapper().readValue(json, new TypeReference<List<HashMap<String, Integer>>>() {}));
    Histogram htg = new Histogram(list);

    assertEquals(0.2, htg.getPercentile(0), 0.0001);
    assertEquals(0.2, htg.getPercentile(25), 0.0001);
    assertEquals(0.3, htg.getPercentile(50), 0.0001);
    assertEquals(0.9, htg.getPercentile(75), 0.0001);
    assertEquals(1677721.6, htg.getPercentile(90), 0.0001);
    assertEquals(1677721.6, htg.getPercentile(100), 0.0001);
  }
}
