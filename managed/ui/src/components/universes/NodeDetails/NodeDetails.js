// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBPanelItem } from '../../panels';
import { YBModal, YBButton } from '../../common/forms/fields';
import { isValidObject, isValidArray } from 'utils/ObjectUtils';


class NodeConnectModal extends Component {
  static propTypes = {
    nodeIPs: PropTypes.array
  }

  constructor(props) {
    super(props);
    this.state = { showConnectModal: false };
    this.toggleConnectModal = this.toggleConnectModal.bind(this);
  }

  toggleConnectModal() {
    this.setState({showConnectModal: !this.state.showConnectModal});
  }

  render() {
    const { nodeIPs } = this.props;

    if (!isValidArray(nodeIPs)) {
      return <span />;
    }
    var privateSSHCommand = nodeIPs.map(function(nodeIP) {
      return(
        <p>
          ssh -i no-such-key.pem centos@{nodeIP.privateIP} -p 54422
        </p>
      )
    })

    var publicSSHCommand = nodeIPs.map(function(nodeIP) {
      return(
        <p>
          ssh -i no-such-key.pem centos@{nodeIP.publicIP} -p 54422
        </p>
      )
    })
    return (
      <div>
        <YBButton btnText={"Connect"}
          btnClass={"btn btn-default"}
          onClick={this.toggleConnectModal}/>
        <YBModal title={"Access your Cluster"}
                 visible={this.state.showConnectModal}
                 onHide={this.toggleConnectModal}
                 showCancelButton={true} cancelLabel={"Ok"}>
          <h4>From Admin host:</h4>
          <ol>
            <li>Locate your private key file (no-such-key.pem).</li>
            <li>Make sure your key is not publicly viewable for SSH to work. Use this command if needed
              <pre>chmod 400 no-such-key.pem</pre>
            </li>
            <li>Connect to your cluster using Private IP:
              <pre>{privateSSHCommand}</pre>
            </li>
          </ol>
          <h4>From localhost: (<i>Not recommended for production</i>)</h4>
          <ol>
            <li>Get your private key file (no-such-key.pem).</li>
            <li>Make sure your key is not publicly viewable for SSH to work. Use this command if needed
              <pre>chmod 400 no-such-key.pem</pre>
            </li>
            <li>Connect to your cluster using Public IP:
              <pre>{publicSSHCommand}</pre>
            </li>
          </ol>
        </YBModal>
      </div>
    );
  }
}

export default class NodeDetails extends Component {
  static propTypes = {
    nodeDetails: PropTypes.array.isRequired
  };

  componentWillReceiveProps(nextProps) {
    if (isValidObject(this.refs.nodeDetailTable)) {
      this.refs.nodeDetailTable.handleSort('asc', 'name');
    }
  }

  render() {
    const { nodeDetails } = this.props;
    if (!isValidArray(nodeDetails)) {
      return <span />;
    }
    const nodeDetailRows = nodeDetails.filter(function(nodeDetail){
        return nodeDetail.state !== "Destroyed"
      }).map(function(nodeDetail){
        return {
          name: nodeDetail.nodeName,
          regionAz: `${nodeDetail.cloudInfo.region}/${nodeDetail.cloudInfo.az}`,
          isMaster: nodeDetail.isMaster ? "Yes" : "No",
          masterPort: nodeDetail.masterHttpPort,
          tserverPort: nodeDetail.tserverHttpPort,
          isTServer: nodeDetail.isTserver ? "Yes" : "No",
          privateIP: nodeDetail.cloudInfo.private_ip,
          publicIP: nodeDetail.cloudInfo.public_ip,
          nodeStatus: nodeDetail.state,
      }
    });

    var formatIpPort = function(cell, row, type) {
      if (cell === "No") {
        return <span>{cell}</span>;
      }
      var href;
      if (type === "master") {
        href = "http://" + row.privateIP + ":" + row.masterPort
      } else {
        href = "http://" + row.privateIP + ":" + row.tserverPort
      }
      return (<a href={href} target="_blank">{cell}</a>);
    }

    const nodeIPs = nodeDetailRows.map(function(node) {
      return { privateIP: node.privateIP, publicIP: node.publicIP }
    });

    return (
      <YBPanelItem name="Node Details">
        <NodeConnectModal nodeIPs={nodeIPs} />
        <BootstrapTable ref='nodeDetailTable' data={nodeDetailRows} >
          <TableHeaderColumn dataField="name" isKey={true}>Instance Name</TableHeaderColumn>
          <TableHeaderColumn dataField="regionAz">Region/Zone</TableHeaderColumn>
          <TableHeaderColumn
              dataField="isMaster"
              dataFormat={ formatIpPort }
              formatExtraData="master" >
            Master
          </TableHeaderColumn>
          <TableHeaderColumn
              dataField="isTServer"
              dataFormat={ formatIpPort }
              formatExtraData="tserver" >
            TServer
          </TableHeaderColumn>
          <TableHeaderColumn dataField="privateIP">Private IP</TableHeaderColumn>
          <TableHeaderColumn dataField="nodeStatus">Node Status</TableHeaderColumn>
        </BootstrapTable>
      </YBPanelItem>
    )
  }
}
