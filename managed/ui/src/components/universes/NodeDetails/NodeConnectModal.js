// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBModal, YBButton } from '../../common/forms/fields';
import { isEmptyArray } from 'utils/ObjectUtils';
import {getPromiseState} from 'utils/PromiseUtils';
import { connect } from 'react-redux';

import './NodeConnectModal.scss';

class NodeConnectModal extends Component {
  static propTypes = {
    nodeIPs: PropTypes.array.isRequired
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
    const { nodeIPs, hostInfo, accessKeys } = this.props;
    if (isEmptyArray(nodeIPs) || !getPromiseState(accessKeys).isSuccess())
     {
      return <span />;
    }

    var accessKey = accessKeys.data[0];
    var accessKeyCode = accessKey.idKey.keyCode;
    var accessKeyInfo = accessKey.keyInfo;
    var privateSSHCommand = nodeIPs.map(function(nodeIP, idx) {
      return(
        <p key={'private-node-details-' + idx}>
          ssh -i { accessKeyInfo.privateKey } centos@{nodeIP.privateIP} -p 54422
        </p>
      )
    })

    var publicSSHCommand = nodeIPs.map(function(nodeIP, idx) {
      return(
        <p key={'public-node-details-' + idx}>
          ssh -i {accessKeyCode}.pem centos@{nodeIP.publicIP} -p 54422
        </p>
      )
    })
    return (
      <div className="node-connect-modal">
        <YBButton btnText={"Connect"} btnClass={"btn btn-default"} onClick={this.toggleConnectModal}/>
        <YBModal title={"Access your Cluster"}
                 visible={this.state.showConnectModal}
                 onHide={this.toggleConnectModal}
                 showCancelButton={true} cancelLabel={"Ok"}>
          <h4>From Admin host: { hostInfo.privateIp }</h4>
          Connect to your cluster using Private IP:
          <pre className={"node-command"}>{ privateSSHCommand }</pre>
          <h4>From localhost: (<i>Not recommended for production</i>)</h4>
          <br />
          Connect to your cluster using Public IP:
          <pre className={"node-command"}>{ publicSSHCommand }</pre>
        </YBModal>
      </div>
    );
  }
}

function mapStateToProps(state, ownProps) {
  return {
    hostInfo: state.customer.hostInfo,
    accessKeys: state.cloud.accessKeys
  };
}

export default connect(mapStateToProps)(NodeConnectModal);
