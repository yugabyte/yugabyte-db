package helpers

import (
  "encoding/json"
  "fmt"
  "io"
  "net/http"
)


type MetricsResponse []MetricsItem


type MetricsItem struct {
  Type       string            `json:"type"`
  ID         string            `json:"id"`
  Attributes MetricsAttributes `json:"attributes"`
  Metrics    []Metric          `json:"metrics"`
}


type MetricsAttributes struct {
  TableType     string `json:"table_type"`
  TableName     string `json:"table_name"`
  NamespaceName string `json:"namespace_name"`
  TableID       string `json:"table_id"`
  StreamID      string `json:"stream_id,omitempty"`
}


type Metric struct {
  Name     string        `json:"name"`
  Min      float64       `json:"min,omitempty"`
  Mean     float64       `json:"mean,omitempty"`
  Max      float64       `json:"max,omitempty"`
  TotalSum float64       `json:"total_sum,omitempty"`
  Value    FlexibleValue `json:"value,omitempty"`
}

type FlexibleValue struct {
  IntValue  int64
  BoolValue bool
  IsBool    bool
  IsInt     bool
}

func (fv *FlexibleValue) UnmarshalJSON(data []byte) error {
  var intValue int64
  if err := json.Unmarshal(data, &intValue); err == nil {
    fv.IntValue = intValue
    fv.IsInt = true
    fv.IsBool = false
    return nil
  }

  var boolValue bool
  if err := json.Unmarshal(data, &boolValue); err == nil {
    fv.BoolValue = boolValue
    fv.IsBool = true
    fv.IsInt = false
    return nil
  }

  return fmt.Errorf("unsupported value type for FlexibleValue: %s", string(data))
}


type MetricsFuture struct {
  MetricsResponse *MetricsResponse
  Error           error
}

func GetMetricsFuture(host, port, metricsParameter string, future chan MetricsFuture) {
  metricsFuture := MetricsFuture{}


  baseURL := fmt.Sprintf("http://%s:%s/metrics?metrics=%s", host, port, metricsParameter)


  req, err := http.NewRequest("GET", baseURL, nil)
  if err != nil {
    metricsFuture.Error = fmt.Errorf("failed to create request: %w", err)
    future <- metricsFuture
    return
  }
  req.Header.Set("Content-Type", "application/json")
  client := &http.Client{}
  resp, err := client.Do(req)
  if err != nil {
    metricsFuture.Error = fmt.Errorf("HTTP request failed: %w", err)
    future <- metricsFuture
    return
  }
  defer resp.Body.Close()

  if resp.StatusCode != http.StatusOK {
    metricsFuture.Error = fmt.Errorf("unexpected response status: %s", resp.Status)
    future <- metricsFuture
    return
  }

  body, err := io.ReadAll(resp.Body)
  if err != nil {
    metricsFuture.Error = fmt.Errorf("failed to read response body: %w", err)
    future <- metricsFuture
    return
  }

  var metricsResponse MetricsResponse
  err = json.Unmarshal(body, &metricsResponse)
  if err != nil {
    metricsFuture.Error = fmt.Errorf("failed to parse JSON: %w", err)
    future <- metricsFuture
    return
  }

  metricsFuture.MetricsResponse = &metricsResponse
  future <- metricsFuture
}
