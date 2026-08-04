package com.yugabyte.troubleshoot.ts.models;

import java.util.Arrays;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.Getter;
import lombok.experimental.Accessors;
import org.apache.commons.lang3.StringUtils;

@Data
@Accessors(chain = true)
public class GraphSettings {

  public static final GraphSettings DEFAULT = new GraphSettings();

  private SplitMode splitMode = SplitMode.NONE;
  private SplitType splitType = SplitType.NONE;
  private int splitCount;
  private boolean returnAggregatedValue = false;
  private Aggregation aggregatedValueFunction = Aggregation.AVG;

  public enum SplitMode {
    NONE,
    TOP,
    BOTTOM
  }

  public enum SplitType {
    NONE,
    NODE,
    TABLE,
    NAMESPACE
  }

  @Getter
  public enum Aggregation {
    DEFAULT(StringUtils.EMPTY),
    MIN("min"),
    MAX("max"),
    AVG("avg"),
    SUM("sum");

    public static final Set<String> AGGREGATION_FUNCTIONS;

    private final String aggregationFunction;

    Aggregation(String aggregationFunction) {
      this.aggregationFunction = aggregationFunction;
    }

    static {
      AGGREGATION_FUNCTIONS =
          Arrays.stream(values())
              .map(Aggregation::getAggregationFunction)
              .filter(StringUtils::isNotEmpty)
              .collect(Collectors.toSet());
    }
  }
}
