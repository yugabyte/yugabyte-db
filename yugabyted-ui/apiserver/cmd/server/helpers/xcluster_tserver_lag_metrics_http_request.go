package helpers

import (
  "encoding/json"
  "io"
  "net/http"
)

type InboundStream struct {
  ReplicationGroupID    string  `json:"replication_group_id"`
  StreamID              string  `json:"stream_id"`
  ConsumerTableID       string  `json:"consumer_table_id"`
  ConsumerTabletID      string  `json:"consumer_tablet_id"`
  ProducerTabletID      string  `json:"producer_tablet_id"`
  State                 string  `json:"state"`
  AvgPollDelayMs        int     `json:"avg_poll_delay_ms"`
  AvgThroughputKiBps    float32 `json:"avg_throughput_KiBps"`
  MiBsReceived          float32 `json:"MiBs_received"`
  RecordsReceived       float32 `json:"records_received"`
  AvgGetChangesLatency  float32 `json:"avg_get_changes_latency_ms"`
  AvgApplyLatencyMs     float32 `json:"avg_apply_latency_ms"`
  ReceivedIndex         float32 `json:"received_index"`
  LastPollTime          string  `json:"last_poll_time"`
  Status                string  `json:"status"`
}

// Response represents the overall API response
type XClusterTserverMetricsResponseFuture struct {
  InboundStreams []InboundStream `json:"inbound_streams"`
  Error   error
}

func (h *HelperContainer) GetXClusterTserverMetricsFuture(nodeHost string,
    future chan XClusterTserverMetricsResponseFuture) {
  response := XClusterTserverMetricsResponseFuture{}
  url := "http://" + nodeHost + ":" + TserverUIPort + "/api/v1/xcluster"
  resp, err := http.Get(url)
  if err != nil {
    response.Error = err
    future <- response
    return
  }
  defer resp.Body.Close()

  body, err := io.ReadAll(resp.Body)
  if err != nil {
    response.Error = err
    future <- response
    return
  }

  err = json.Unmarshal(body, &response)
  if err != nil {
    response.Error = err
  }
  future <- response
}
