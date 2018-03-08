// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBModal, YBButton } from '../../common/forms/fields';
import { isEmptyArray } from 'utils/ObjectUtils';
import {getPromiseState} from 'utils/PromiseUtils';
import { connect } from 'react-redux';
import { isNonEmptyObject } from "../../../utils/ObjectUtils";

import './NodeConnectModal.scss';

class NodeConnectModal extends Component {
  static propTypes = {
    nodeIPs: PropTypes.array.isRequired
  };

  constructor(props) {
    super(props);
    this.state = { showConnectModal: false };
  }

  toggleConnectModal = () => {
    this.setState({showConnectModal: !this.state.showConnectModal});
  };

  render() {
    const { nodeIPs, hostInfo, accessKeys, providerUUID } = this.props;
    if (isEmptyArray(nodeIPs) || !getPromiseState(accessKeys).isSuccess()) {
      return <span />;
    }

    let fromAdminMsg = "From Admin host: ";
    let hostPrivateIP = "";
    if (hostInfo) {
      Object.keys(hostInfo).forEach(function(hostProviderKey){
        if (!hostInfo[hostProviderKey].error) {
          hostPrivateIP = hostInfo[hostProviderKey].privateIp;
        }
      });
    }
    if (hostInfo && !hostInfo.error) {
      fromAdminMsg += hostPrivateIP;
    }

    const accessKey = accessKeys.data.filter((key) => key.idKey.providerUUID === providerUUID)[0];
    if (!isNonEmptyObject(accessKey)) {
      return <span/>;
    }
    const accessKeyCode = accessKey.idKey.keyCode;
    const accessKeyInfo = accessKey.keyInfo;
    const privateSSHCommand = nodeIPs.map(function(nodeIP, idx) {
      return(
        <p key={'private-node-details-' + idx}>
          sudo ssh -i { accessKeyInfo.privateKey } centos@{nodeIP.privateIP} -p 54422
        </p>
      );
    });

    const publicSSHCommand = nodeIPs.map(function(nodeIP, idx) {
      return(
        <p key={'public-node-details-' + idx}>
          sudo ssh -i {accessKeyCode}.pem centos@{nodeIP.publicIP} -p 54422
        </p>
      );
    });
    return (
      <div className="node-connect-modal">
        <YBButton btnText={"Connect"} btnClass={"btn btn-default"} onClick={this.toggleConnectModal}/>
        <YBModal title={"Access your Cluster"}
                 visible={this.state.showConnectModal}
                 onHide={this.toggleConnectModal}
                 showCancelButton={true} cancelLabel={"Ok"}>
          <h4>{ fromAdminMsg }</h4>
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
