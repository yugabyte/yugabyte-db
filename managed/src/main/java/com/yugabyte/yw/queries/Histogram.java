package com.yugabyte.yw.queries;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import lombok.Getter;
import play.libs.Json;

@Getter
public class Histogram {

  private TreeMap<String, Integer> bins;
  private int totalCount;

  public Histogram(List<Map<String, Integer>> list) {

    this.bins =
        new TreeMap<>(
            new Comparator<String>() {
              @Override
              public int compare(String range1, String range2) {
                Double rangeStart1 = Double.parseDouble(range1.substring(1, range1.indexOf(",")));
                Double rangeStart2 = Double.parseDouble(range2.substring(1, range2.indexOf(",")));
                return rangeStart1.compareTo(rangeStart2);
              }
            });

    for (Map<String, Integer> rangeMap : list) {
      String range = rangeMap.keySet().stream().findFirst().get();
      Integer count = rangeMap.get(range);
      bins.put(range, count);
      totalCount += count;
    }
  }

  public void merge(Histogram other) {
    other
        .getBins()
        .forEach(
            (binName, count) -> {
              int count_1 = this.bins.getOrDefault(binName, 0);
              this.bins.put(binName, count_1 + count);
              this.totalCount += count;
            });
  }

  public ArrayNode getArrayNode() {
    ArrayNode arrayNode = Json.mapper().createArrayNode();
    bins.forEach(
        (binName, count) -> {
          ObjectNode obj = Json.mapper().createObjectNode();
          obj.put(binName, count);
          arrayNode.add(obj);
        });
    return arrayNode;
  }

  public double getPercentile(double percentile) {
    percentile = percentile < 100 ? percentile : 100;
    int count = 0;
    for (Map.Entry<String, Integer> bin : bins.entrySet()) {
      count += bin.getValue();
      if (((double) count / (double) totalCount) * 100.0 >= percentile) {
        String[] binValues = bin.getKey().replace("[", "").replace(")", "").split(",");
        double binStart = Double.parseDouble(binValues[0]);
        // corner case for last bucket
        if (binValues.length == 1) {
          return binStart;
        }
        double binEnd = Double.parseDouble(binValues[1]);
        return binEnd;
      }
    }
    return Double.NaN;
  }

  public String toString() {
    return bins.toString() + "total count = " + totalCount;
  }
}
