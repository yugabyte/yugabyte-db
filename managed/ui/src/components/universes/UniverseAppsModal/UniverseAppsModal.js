// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBModal, YBButton } from '../../common/forms/fields';
import { YBCodeBlock, YBCopyButton } from '../../common/descriptors';
import { isValidObject, isEmptyObject } from '../../../utils/ObjectUtils';
import { Tab, Tabs } from 'react-bootstrap';
import { isKubernetesUniverse } from '../../../utils/UniverseUtils';

import './UniverseAppsModal.scss';

const appTypes = [
  {
    code: 'SqlInserts',
    type: 'ysql',
    title: 'YSQL',
    description:
      'This app writes out 2M unique string keys each with a string value. There are multiple ' +
      'readers and writers that write 2M keys and read 1.5M keys . ' +
      'To write the keys and read them indefinitely set num_reads & num_writes to -1 . Note that the number of ' +
      'reads and writes to perform can be specified as a parameter.',
    options: [
      { num_unique_keys: '2000000' },
      { num_reads: '1500000' },
      { num_writes: '2000000' },
      { num_threads_read: '32' },
      { num_threads_write: '2' }
    ]
  },
  {
    code: 'CassandraKeyValue',
    type: 'cassandra',
    title: 'YCQL',
    description:
      'This app writes out 2M unique string keys ' +
      'each with a string value. There are multiple readers and writers that update 2M ' +
      'keys and read 1.5M keys. ' +
      'To update the keys and read them indefinitely set num_reads & num_writes to -1 .' +
      'Note that the number of reads and writes to ' +
      'perform can be specified as a parameter.',
    options: [
      { num_unique_keys: '2000000' },
      { num_reads: '1500000' },
      { num_writes: '2000000' },
      { num_threads_read: '24' },
      { num_threads_write: '2' },
      { table_ttl_seconds: '-1' }
    ]
  }
];

export default class UniverseAppsModal extends Component {
  static propTypes = {
    currentUniverse: PropTypes.object.isRequired,
    button: PropTypes.node.isRequired,
    modal: PropTypes.object.isRequired
  };

  render() {
    const {
      currentUniverse: { universeDetails, sampleAppCommandTxt },
      button,
      closeModal,
      modal: { showModal, visibleModal }
    } = this.props;
    const enableYSQL = universeDetails.clusters[0].userIntent.enableYSQL;
    const isItKubernetesUniverse = isKubernetesUniverse(this.props.currentUniverse);
    const nodeDetails = universeDetails.nodeDetailsSet
      ? universeDetails.nodeDetailsSet.filter((nodeDetails) => nodeDetails.isTserver)
      : [];

    const getHost = function (host) {
      return host !== '127.0.0.1' ? host : 'host.docker.internal';
    };

    const cassandraHosts = nodeDetails
      .map(function (nodeDetail) {
        if (
          nodeDetail.state === 'Live' &&
          nodeDetail.cloudInfo &&
          isValidObject(nodeDetail.cloudInfo.private_ip)
        )
          return getHost(nodeDetail.cloudInfo.private_ip) + ':' + nodeDetail.yqlServerRpcPort;
        else return null;
      })
      .filter(Boolean)
      .join(',');
    const redisHosts = nodeDetails
      .map(function (nodeDetail) {
        if (
          nodeDetail.state === 'Live' &&
          nodeDetail.cloudInfo &&
          isValidObject(nodeDetail.cloudInfo.private_ip)
        )
          return getHost(nodeDetail.cloudInfo.private_ip) + ':' + nodeDetail.redisServerRpcPort;
        else return null;
      })
      .filter(Boolean)
      .join(',');
    const ysqlHosts = nodeDetails
      .map(function (nodeDetail) {
        if (
          nodeDetail.state === 'Live' &&
          nodeDetail.cloudInfo &&
          isValidObject(nodeDetail.cloudInfo.private_ip)
        )
          return getHost(nodeDetail.cloudInfo.private_ip) + ':' + nodeDetail.ysqlServerRpcPort;
        else return null;
      })
      .filter(Boolean)
      .join(',');

    const appTabs = appTypes.map(function (appType, idx) {
      let hostPorts;
      let betaFeature = '';

      switch (appType.type) {
        case 'cassandra':
          hostPorts = cassandraHosts;
          break;
        case 'redis':
          hostPorts = redisHosts;
          break;
        case 'ysql':
          hostPorts = ysqlHosts;
          if (!enableYSQL)
            betaFeature =
              'NOTE: This is a beta feature. If you want to try out the app, ' +
              'create a universe with YSQL enabled.';
          break;
        default:
          break;
      }

      const appOptions = appType.options.map(function (option, idx) {
        const option_data = Object.entries(option).shift();
        return <p key={idx}>--{option_data[0] + ' ' + option_data[1]}</p>;
      });

      const commandSyntax = isItKubernetesUniverse
        ? 'kubectl run --image=yugabytedb/yb-sample-apps yb-sample-apps --'
        : 'docker run -d yugabytedb/yb-sample-apps';

      const command =
        appType.title === 'YCQL'
          ? sampleAppCommandTxt
          : commandSyntax +
            ' --workload ' +
            appType.code +
            ' --nodes ' +
            hostPorts +
            ' --username yugabyte --password <your_password> ';

      return (
        <Tab eventKey={idx} title={appType.title} key={appType.code}>
          {betaFeature}
          <label className="app-description">{appType.description}</label>
          <YBCodeBlock label="Usage:">
            {command}
            <YBCopyButton text={command} />
          </YBCodeBlock>
          <YBCodeBlock label="Other options (with default values):">{appOptions}</YBCodeBlock>
        </Tab>
      );
    });

    return (
      <Fragment>
        {isEmptyObject(button) ? (
          <YBButton
            btnText={'Run Sample Apps'}
            btnClass={'btn btn-default open-modal-btn'}
            onClick={this.toggleAppsModal}
          />
        ) : (
          button
        )}
        <YBModal
          className="universe-apps-modal"
          title={'Run Sample Apps'}
          visible={showModal && visibleModal === 'runSampleAppsModal'}
          onHide={closeModal}
        >
          <Tabs defaultActiveKey={0} id="apps-modal">
            {appTabs}
          </Tabs>
        </YBModal>
      </Fragment>
    );
  }
}
