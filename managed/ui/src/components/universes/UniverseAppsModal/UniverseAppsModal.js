// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
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
  { code: "RedisKeyValue", type: "redis", title: "Redis Key Value",
    description: "This app writes out 1M unique string keys each with a string value. There are multiple "+
    "readers and writers that update these keys and read them indefinitely. Note that the number of " +
    "reads and writes to perform can be specified as a parameter.",
    options: [{"num_unique_keys": "1000000"}, {"num_reads": "-1"}, {"num_writes": "-1"},
    {"num_threads_read": "32"}, {"num_threads_write": "2"}]
  },
  { code: "SqlInserts", type: "ysql", title: "Sql Inserts",
    description: "This app writes out 1M unique string keys each with a string value. There are multiple "+
        "readers and writers that write these keys and read them indefinitely. Note that the number of " +
        "reads and writes to perform can be specified as a parameter.",
    options: [{"num_unique_keys": "1000000"}, {"num_reads": "-1"}, {"num_writes": "-1"},
        {"num_threads_read": "32"}, {"num_threads_write": "2"}]
  }
];

export default class UniverseAppsModal extends Component {
  static propTypes = {
    nodeDetails: PropTypes.array.isRequired
  };
  constructor(props) {
    super(props);
    this.state = { showAppsModal: false };
  }

  toggleAppsModal = () => {
    this.setState({showAppsModal: !this.state.showAppsModal});
  };

  render() {
    const { enableYSQL, nodeDetails } = this.props;
    const cassandraHosts = nodeDetails.map(function(nodeDetail) {
      if (nodeDetail.state === "Live" && nodeDetail.cloudInfo && isValidObject(nodeDetail.cloudInfo.private_ip))
        return nodeDetail.cloudInfo.private_ip + ":" + nodeDetail.yqlServerRpcPort;
      else
        return null;
    }).filter(Boolean).join(",");
    const redisHosts = nodeDetails.map(function(nodeDetail) {
      if (nodeDetail.state === "Live" && nodeDetail.cloudInfo && isValidObject(nodeDetail.cloudInfo.private_ip))
        return nodeDetail.cloudInfo.private_ip + ":" + nodeDetail.redisServerRpcPort;
      else
        return null;
    }).filter(Boolean).join(",");
    const ysqlHosts = nodeDetails.map(function(nodeDetail) {
      if (nodeDetail.state === "Live" && nodeDetail.cloudInfo && isValidObject(nodeDetail.cloudInfo.private_ip))
        return nodeDetail.cloudInfo.private_ip + ":" + nodeDetail.ysqlServerRpcPort;
      else
        return null;
    }).filter(Boolean).join(",");

    const appTabs = appTypes.map(function(appType, idx) {
      let hostPorts;
      let betaFeature = "";

      switch (appType.type) {
        case "cassandra":
          hostPorts = cassandraHosts;
          break;
        case "redis":
          hostPorts = redisHosts;
          break;
        case "ysql":
          hostPorts = ysqlHosts;
          if (!enableYSQL)
            betaFeature = "NOTE: This is a beta feature. If you want to try out the Sql Inserts app, " +
                          "create a universe with YSQL enabled.";
          break;
        default:
          break;
      }

      const appOptions = appType.options.map(function(option, idx) {
        const option_data = Array.shift(Object.entries(option));
        return <p key={idx}>--{ option_data[0] + " " + option_data[1]}</p>;
      });
      return (
        <Tab eventKey={idx} title={appType.title} key={appType.code}>
          {betaFeature}
          <label className="app-description">{appType.description}</label>
          <YBCodeBlock label="Usage:">
            java -jar /opt/yugabyte/utils/yb-sample-apps.jar --workload {appType.code} --nodes {hostPorts}
          </YBCodeBlock>
          <YBCodeBlock label="Other options (with default values):">
            {appOptions}
          </YBCodeBlock>
        </Tab>);
    });

    return (
      <div className="universe-apps-modal">
        <YBButton btnText={"Run Sample Apps"} btnClass={"btn btn-default open-modal-btn"} onClick={this.toggleAppsModal}/>
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
