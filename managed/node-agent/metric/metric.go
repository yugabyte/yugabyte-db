// Copyright (c) YugaByte, Inc.

package metric

import (
	"net/http"
	"node-agent/util"
	"sync"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
	"google.golang.org/grpc"
)

var (
	instance *Metrics
	once     = &sync.Once{}
)

func init() {
	instance = newMetrics()
}

// GetInstance returns the singleton metrics.
func GetInstance() *Metrics {
	return instance
}

// Metrics struct contains all the metrics.
type Metrics struct {
	uptimeCounter     *prometheus.CounterVec
	invocationCounter *prometheus.CounterVec
	responseHistogram *prometheus.HistogramVec
}

func newMetrics() *Metrics {
	metrics := &Metrics{
		// Start of all metrics.
		uptimeCounter: prometheus.NewCounterVec(
			prometheus.CounterOpts{
				Name: "nodeagent_uptime_total",
				Help: "Total number of uptime heartbeats.",
			}, []string{"host", "uuid"}),
		invocationCounter: prometheus.NewCounterVec(
			prometheus.CounterOpts{
				Name: "nodeagent_rpc_total",
				Help: "Total number of rpc invocations.",
			}, []string{"host", "uuid", "service", "method", "response_code"}),
		responseHistogram: prometheus.NewHistogramVec(prometheus.HistogramOpts{
			Name:    "nodeagent_response_seconds",
			Help:    "Histogram of response time of RPC methods.",
			Buckets: prometheus.DefBuckets,
		}, []string{"host", "uuid", "service", "method"}),
		// End of all metrics.
	}
	// Register this collector.
	prometheus.MustRegister(metrics)
	return metrics
}

// HTTPHandler returns the HTTP handler.
func (metrics *Metrics) HTTPHandler() http.Handler {
	return promhttp.Handler()
}

// PrepopulateMetrics prepopulates metrics.
func (metrics *Metrics) PrepopulateMetrics(server *grpc.Server) {
	sLabelValues := metrics.serverLabelValues()
	metrics.prepopulateServerMetrics(sLabelValues...)
	serviceInfo := server.GetServiceInfo()
	for sName, info := range serviceInfo {
		for _, mInfo := range info.Methods {
			labelValues := append([]string{}, sLabelValues...)
			labelValues = append(labelValues, sName, mInfo.Name)
			metrics.prepopulateMethodMetrics(labelValues...)
		}
	}
}

func (metrics *Metrics) serverLabelValues() []string {
	config := util.CurrentConfig()
	return []string{config.String(util.NodeIpKey), config.String(util.NodeAgentIdKey)}
}

// initServerMetrics allows the metrics to be prepopulated.
func (metrics *Metrics) prepopulateServerMetrics(labelValues ...string) {
	metrics.uptimeCounter.GetMetricWithLabelValues(labelValues...)
}

// initMethodMetrics allows the metrics to be prepopulated.
func (metrics *Metrics) prepopulateMethodMetrics(labelValues ...string) {
	// TODO for others
	metrics.responseHistogram.GetMetricWithLabelValues(labelValues...)
}

// heartbeat publishes heartbeat.
func (metrics *Metrics) heartbeat() {
	metrics.incrementCounter(metrics.uptimeCounter)
}

// Describe implements the method in prometheus Collector.
func (metrics *Metrics) Describe(ch chan<- *prometheus.Desc) {
	metrics.uptimeCounter.Describe(ch)
	metrics.invocationCounter.Describe(ch)
	metrics.responseHistogram.Describe(ch)
}

// Collect implements the method in prometheus Collector.
func (metrics *Metrics) Collect(ch chan<- prometheus.Metric) {
	metrics.heartbeat()
	metrics.uptimeCounter.Collect(ch)
	metrics.invocationCounter.Collect(ch)
	metrics.responseHistogram.Collect(ch)
}

// incrementCounter increments the given counter.
func (metrics *Metrics) incrementCounter(
	counter *prometheus.CounterVec,
	labelValues ...string,
) {
	lVals := metrics.serverLabelValues()
	lVals = append(lVals, labelValues...)
	counter.WithLabelValues(lVals...).Inc()
}

// observeHistogram updates the given histogram.
func (metrics *Metrics) observeHistogram(
	histogram *prometheus.HistogramVec,
	value float64,
	labelValues ...string,
) {
	lVals := metrics.serverLabelValues()
	lVals = append(lVals, labelValues...)
	histogram.WithLabelValues(lVals...).Observe(value)
}

// PublishServerMethodStats publishes RPC server related metrics.
func (metrics *Metrics) PublishServerMethodStats(
	elapsed time.Duration,
	sName, mName, responseCode string,
) {
	metrics.incrementCounter(metrics.invocationCounter, sName, mName, responseCode)
	metrics.observeHistogram(
		metrics.responseHistogram,
		float64(elapsed.Milliseconds()),
		sName,
		mName,
	)
}
