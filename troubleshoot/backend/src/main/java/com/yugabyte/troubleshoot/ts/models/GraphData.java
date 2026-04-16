package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.text.DecimalFormat;
import java.text.DecimalFormatSymbols;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.experimental.Accessors;
import org.apache.commons.lang3.StringUtils;

@Data
@Accessors(chain = true)
@Builder(toBuilder = true)
@NoArgsConstructor
@AllArgsConstructor
public class GraphData {
  private static DecimalFormat DF =
      new DecimalFormat("0", DecimalFormatSymbols.getInstance(Locale.ENGLISH));

  {
    DF.setMaximumFractionDigits(10);
  }

  private String name;
  private String instanceName;
  private String tableName;
  private String tableId;
  private String namespaceName;
  private String namespaceId;
  private String nodePrefix;
  private String type;
  private String waitEventComponent;
  private String waitEventClass;
  private String waitEventType;
  private String waitEvent;
  private Map<String, String> labels;
  @JsonIgnore @Builder.Default private List<GraphPoint> points = new ArrayList<>();
  @JsonIgnore private double total;

  public void appendToTotal(Double value) {
    total += value;
  }

  @JsonIgnore
  public Double getAverage() {
    return total / points.size();
  }

  @JsonProperty("x")
  public List<Long> getX() {
    return points.stream().map(GraphPoint::getX).toList();
  }

  @JsonProperty("y")
  public List<String> getY() {
    return points.stream().map(GraphPoint::getY).map(DF::format).toList();
  }

  // TODO These methods are needed to properly parse mocked response.
  // Will remove once mocked response functionality will be removed.
  @JsonProperty("x")
  public void setX(List<Long> x) {
    if (points.isEmpty()) {
      points = x.stream().map(v -> new GraphPoint().setX(v)).toList();
    } else {
      for (int i = 0; i < x.size(); i++) {
        points.get(i).setX(x.get(i));
      }
    }
  }

  @JsonProperty("y")
  public void setY(List<String> y) {
    if (points.isEmpty()) {
      points = y.stream().map(v -> new GraphPoint().setY(Double.parseDouble(v))).toList();
    } else {
      for (int i = 0; i < y.size(); i++) {
        points.get(i).setY(Double.parseDouble(y.get(i)));
      }
    }
  }

  @JsonIgnore
  public String getInstanceNameOrEmpty() {
    return instanceName != null ? instanceName : StringUtils.EMPTY;
  }

  @JsonIgnore
  public String getNameOrEmpty() {
    return name != null ? name : StringUtils.EMPTY;
  }

  @JsonIgnore
  public String getWaitEventClassOrEmpty() {
    return waitEventClass != null ? waitEventClass : StringUtils.EMPTY;
  }

  @JsonIgnore
  public String getWaitEventOrEmpty() {
    return waitEvent != null ? waitEvent : StringUtils.EMPTY;
  }
}
