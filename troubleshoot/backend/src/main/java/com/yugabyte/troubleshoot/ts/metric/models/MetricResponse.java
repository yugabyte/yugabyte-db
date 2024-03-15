package com.yugabyte.troubleshoot.ts.metric.models;

import com.fasterxml.jackson.annotation.JsonValue;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.stream.Collectors;
import lombok.Getter;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

@Getter
public class MetricResponse {
  private Status status;
  private Data data;

  @Getter
  public static class Data {
    private ResultType resultType;
    private List<Result> result;
  }

  public static class Result {
    @Getter private Map<String, String> metric;
    private List<Object> value;
    private List<List<Object>> values;

    public Pair<Double, Double> getValue() {
      return getValue(value);
    }

    public List<Pair<Double, Double>> getValues() {
      if (CollectionUtils.isEmpty(values)) {
        return null;
      }
      return values.stream()
          .map(this::getValue)
          .filter(Objects::nonNull)
          .collect(Collectors.toList());
    }

    private Pair<Double, Double> getValue(List<Object> value) {
      if (CollectionUtils.isEmpty(value)) {
        return null;
      }
      return new ImmutablePair<>((Double) value.get(0), Double.valueOf((String) value.get(1)));
    }
  }

  public enum ResultType {
    MATRIX("matrix"),
    VECTOR("vector"),
    SCALAR("scalar"),
    STRING("string");

    private final String value;

    ResultType(String value) {
      this.value = value;
    }

    @JsonValue
    public String getValue() {
      return value;
    }
  }

  public enum Status {
    SUCCESS("success"),
    ERROR("error");

    private final String value;

    Status(String value) {
      this.value = value;
    }

    @JsonValue
    public String getValue() {
      return value;
    }
  }
}
