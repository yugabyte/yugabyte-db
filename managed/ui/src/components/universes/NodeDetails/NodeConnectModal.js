// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBModal } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { connect } from 'react-redux';
import { isEmptyObject } from '../../../utils/ObjectUtils';
import { getPrimaryCluster, getReadOnlyCluster } from '../../../utils/UniverseUtils';
import { YBCopyButton } from '../../../components/common/descriptors';
import { MenuItem } from 'react-bootstrap';

import './NodeConnectModal.scss';

import _ from 'lodash';

class NodeConnectModal extends Component {
  static propTypes = {
    currentRow: PropTypes.object,
    label: PropTypes.element,
    providerUUID: PropTypes.string.isRequired
  };

  static defaultProps = {
    currentRow: {}
  };

  constructor(props) {
    super(props);
    this.state = { showConnectModal: false };
  }

  toggleConnectModal = (value) => {
    this.setState({ showConnectModal: value });
  };

  render() {
    const {
      currentRow,
      label,
      accessKeys,
      providerUUID,
      runtimeConfigs,
      currentUniverse,
      clusterType
    } = this.props;
    const nodeIPs = { privateIP: currentRow.privateIP, publicIP: currentRow.publicIP };
    let accessCommand = null;
    let accessTitle = null;
    const cluster =
      clusterType === 'primary'
        ? getPrimaryCluster(currentUniverse.data?.universeDetails?.clusters)
        : getReadOnlyCluster(currentUniverse.data?.universeDetails?.clusters);
    if (
      (isEmptyObject(nodeIPs) || currentRow.cloudInfo.cloud !== 'kubernetes') &&
      !getPromiseState(accessKeys).isSuccess()
    ) {
      return <MenuItem>{label}</MenuItem>;
    }

    const accessKey = accessKeys.data.filter((key) => key.idKey.providerUUID === providerUUID)[0];
    if (isEmptyObject(accessKey) && currentRow.cloudInfo.cloud !== 'kubernetes') {
      return <span />;
    }

    const tectiaSSH = runtimeConfigs?.data?.configEntries?.find(
      (c) => c.key === 'yb.security.ssh2_enabled'
    );
    let isTectiaSSHEnabled = false;

    if (tectiaSSH?.value === 'true') {
      isTectiaSSHEnabled = true;
    }

    if (currentRow.cloudInfo.cloud === 'kubernetes') {
      accessTitle = 'Access your pod';
      const podNamespace = currentRow.cloudInfo.kubernetesNamespace
        ? currentRow.cloudInfo.kubernetesNamespace
        : currentRow.privateIP?.split('.')[2];
      const podName = currentRow.cloudInfo.kubernetesPodName
        ? currentRow.cloudInfo.kubernetesPodName
        : currentRow.privateIP?.split('.')[0];
      let container_name_selector = '';

      if (currentRow.isMaster === 'Details') {
        container_name_selector = '-c yb-master';
      } else if (currentRow.isTServer === 'Details') {
        container_name_selector = '-c yb-tserver';
      }

      accessCommand = `kubectl exec -it -n ${podNamespace} ${podName} ${container_name_selector} -- sh`;
    } else {
      accessTitle = 'Access your node';
      const accessKeyCode = cluster.userIntent.accessKeyCode;
      const accessKey = accessKeys.data.find(
        (key) => key.idKey.providerUUID === providerUUID && key.idKey.keyCode === accessKeyCode
      );

      const accessKeyInfo = accessKey?.keyInfo;
      const sshPort = accessKeyInfo?.sshPort || 22;
      if (!isTectiaSSHEnabled) {
        accessCommand = `sudo ssh -i ${
          accessKeyInfo?.privateKey ?? '<private key>'
        } -ostricthostkeychecking=no -p ${sshPort} yugabyte@${nodeIPs.privateIP}`;
      } else {
        accessCommand = `sshg3 -K ${
          accessKeyInfo?.privateKey ?? '<private key>'
        } -ostricthostkeychecking=no -p ${sshPort} yugabyte@${nodeIPs.privateIP}`;
      }
    }

    const btnId = _.uniqueId('node_action_btn_');
    return (
      <Fragment>
        <MenuItem eventKey={btnId} onClick={() => this.toggleConnectModal(true)}>
          {label}
        </MenuItem>
        <YBModal
          title={accessTitle}
          visible={this.state.showConnectModal}
          onHide={() => this.toggleConnectModal(false)}
          showCancelButton={true}
          cancelLabel={'OK'}
        >
          <pre className={'node-command'}>
            <code>{accessCommand}</code>
            <YBCopyButton text={accessCommand}></YBCopyButton>
          </pre>
        </YBModal>
      </Fragment>
    );
  }
}

function mapStateToProps(state) {
  return {
    accessKeys: state.cloud.accessKeys,
    runtimeConfigs: state.customer.runtimeConfigs,
    currentUniverse: state.universe.currentUniverse
  };
}

export default connect(mapStateToProps)(NodeConnectModal);
