// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBModal, YBButton } from '../../common/forms/fields';
import { YBCodeBlock } from '../../common/descriptors';
import { isValidObject } from 'utils/ObjectUtils';
import { Tab, Tabs } from 'react-bootstrap';

import './UniverseAppsModal.scss';

const appTypes = [
  { code: "CassandraKeyValue", type: "cassandra", title: "Cassandra Key Value",
    description: "This app writes out 1M unique string keys " +
    "each with a string value. There are multiple readers and writers that update these " +
		"keys and read them indefinitely. Note that the number of reads and writes to " +
		"perform can be specified as a parameter.",
    options: [{"num_unique_keys": "1000000"}, {"num_reads": "-1"}, {"num_writes": "-1"},
    {"num_threads_read": "24"}, {"num_threads_write": "2"}, {"table_ttl_seconds": "-1"}]
  },
  { code: "CassandraStockTicker", type: "cassandra", title: "Cassandra Stock Ticker",
    description: "This app models 10,000 stock tickers	each of which emits quote data every second. " +
    "The raw data is written into the 'stock_ticker_raw' table, which retains data for one day. " +
    "The 'stock_ticker_1min' table models downsampled ticker data, is written to once a minute " +
    "and retains data for 60 days. Every read query gets the latest value of the stock symbol from " +
    "the 'stock_ticker_raw' table.",
    options: [{"num_ticker_symbols": "10000"}, {"data_emit_rate_millis": "1000"},
    {"num_threads_read": "32"}, {"num_threads_write": "4"}, {"table_ttl_seconds": "86400"}]
  },
  { code: "CassandraTimeseries", type: "cassandra", title: "Cassandra Timeseries",
    description: "This app models 100 users, each of whom own 5-10 devices. Each device emits " +
    "5-10 metrics per second. The data is written into the 'ts_metrics_raw' table, which retains data " +
    "for one day. Note that the number of metrics written is a lot more than the number of metrics read " +
    "as is typical in such workloads. Every read query fetches the last 1-3 hours of metrics for a user's device.",
    options: [{"num_users": "100"}, {"min_nodes_per_user": "5"},{"max_nodes_per_user": "10"},
    {"min_metrics_count": "5"}, {"max_metrics_count": "10"},{"data_emit_rate_millis": "1000"},
    {"num_threads_read": "1"}, {"num_threads_write": "16"}, {"table_ttl_seconds": "86400"}]
  },
  { code: "RedisKeyValue", type: "redis", title: "Redis Key Value",
    description: "This app writes out 1M unique string keys each with a string value. There are multiple "+
    "readers and writers that update these keys and read them indefinitely. Note that the number of " +
    "reads and writes to perform can be specified as a parameter.",
    options: [{"num_unique_keys": "1000000"}, {"num_reads": "-1"}, {"num_writes": "-1"},
    {"num_threads_read": "32"}, {"num_threads_write": "2"}]
  }
]

export default class UniverseAppsModal extends Component {
  static propTypes = {
    nodeDetails: PropTypes.array.isRequired
  };
  constructor(props) {
    super(props);
    this.state = { showAppsModal: false };
    this.toggleAppsModal = this.toggleAppsModal.bind(this);
  }

  toggleAppsModal() {
    this.setState({showAppsModal: !this.state.showAppsModal});
  }

  render() {
    const { nodeDetails } = this.props;
    var cassandraHosts = nodeDetails.map(function(nodeDetail) {
      if (nodeDetail.state === "Running" && isValidObject(nodeDetail.cloudInfo.private_ip))
        return nodeDetail.cloudInfo.private_ip + ":" + nodeDetail.yqlServerRpcPort
      else
        return null
    }).filter(Boolean).join(",")
    var redisHosts = nodeDetails.map(function(nodeDetail) {
      if (nodeDetail.state === "Running" && isValidObject(nodeDetail.cloudInfo.private_ip))
        return nodeDetail.cloudInfo.private_ip + ":" + nodeDetail.redisServerRpcPort
      else
        return null
    }).filter(Boolean).join(",")

    var appTabs = appTypes.map(function(appType, idx) {
      var hostPorts = cassandraHosts;
      if (appType.type === "redis") {
        hostPorts = redisHosts;
      }

      var appOptions = appType.options.map(function(option, idx) {
        var option_data = Array.shift(Object.entries(option));
        return (<p key={idx}>--{ option_data[0] + " " + option_data[1]}</p>);
      })
      return (
        <Tab eventKey={idx} title={appType.title} key={appType.code}>
          <label className="app-description">{appType.description}</label>
          <YBCodeBlock label="Usage:">
            java -jar yb-sample-app.jar --workload {appType.code} --nodes {hostPorts}
          </YBCodeBlock>
          <YBCodeBlock label="Other options (with default values):">
            {appOptions}
          </YBCodeBlock>
        </Tab>)
    })

    return (
      <div className="universe-apps-modal">
        <YBButton btnText={"Apps"} btnClass={"btn btn-default open-modal-btn"} onClick={this.toggleAppsModal}/>
        <YBModal title={"Run Sample Apps"} visible={this.state.showAppsModal}
                 onHide={this.toggleAppsModal} className={"universe-apps-modal"}>
          <Tabs defaultActiveKey={0} id="apps-modal">
            {appTabs}
          </Tabs>
        </YBModal>
      </div>
    );
  }
}
