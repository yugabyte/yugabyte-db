// Copyright (c) YugaByte, Inc.
import { Component } from 'react';
import { Alert, Row, Panel } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import moment from 'moment-timezone';
import { sortBy, values } from 'lodash';
import { NodeAgentStatusModal } from '../../../configRedesign/providerRedesign/providerView/instanceTypes/NodeAgentStatusModal';
import { YBLoading } from '../../../common/indicators';
import TreeNode from '../../../common/TreeNode';
import { YBPanelItem } from '../../../panels';
import { isNonEmptyArray, isEmptyArray, isNonEmptyString } from '../../../../utils/ObjectUtils';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import { UniverseAction } from '../../../universes';
import { isDisabled, isNotHidden } from '../../../../utils/LayoutUtils';
import { getPrimaryCluster } from '../../../../utils/UniverseUtils';
import Wrench from '../../../../redesign/assets/wrench.svg';

import './UniverseHealthCheckList.scss';

export const UniverseHealthCheckList = (props) => {
  const {
    universe: { healthCheck, currentUniverse },
    currentCustomer,
    currentUser
  } = props;
  const primaryCluster = getPrimaryCluster(
    props?.universe?.currentUniverse?.data?.universeDetails?.clusters
  );
  const useSystemd = primaryCluster?.userIntent?.useSystemd;
  let nodesCronStatus = <span />;
  const inactiveCronNodes = getNodesWithInactiveCrons(currentUniverse.data).join(', ');
  if (!useSystemd && isNonEmptyString(inactiveCronNodes)) {
    nodesCronStatus = (
      <Alert bsStyle="warning" className="pre-provision-message">
        Warning: cronjobs are not active on some nodes ({inactiveCronNodes})
      </Alert>
    );
  }

  let content = <span />;
  if (getPromiseState(healthCheck).isLoading()) {
    content = <YBLoading />;
  } else if (
    getPromiseState(healthCheck).isEmpty() ||
    getPromiseState(healthCheck).isError() ||
    (getPromiseState(healthCheck).isSuccess() && isEmptyArray(healthCheck.data))
  ) {
    content = <div>{"There're no finished Health checks available at the moment."}</div>;
  } else if (getPromiseState(healthCheck).isSuccess() && isNonEmptyArray(healthCheck.data)) {
    const data = [...healthCheck.data].reverse();
    const timestamps = prepareData(data, currentUser.data.timezone);
    content = timestamps.map((timestamp, index) => (
      <Timestamp
        id={'healthcheck' + timestamp.timestampMoment.unix()}
        key={timestamp.timestampMoment.unix()}
        timestamp={timestamp}
        index={index}
        universeDetails={currentUniverse?.data?.universeDetails}
      />
    ));
  }

  const actions_disabled = isDisabled(currentCustomer.data.features, 'universes.actions');

  return (
    <YBPanelItem
      className="UniverseHealthCheckList"
      header={
        <div className="clearfix">
          <Row>
            <div className="pull-left">
              <h2>Health Checks</h2>
            </div>
            {isNotHidden(currentCustomer.data.features, 'universes.details.health.alerts') && (
              <div className="pull-right">
                <div className="backup-action-btn-group">
                  <UniverseAction
                    className="table-action"
                    universe={currentUniverse.data}
                    actionType="alert-config"
                    btnClass={'btn-orange'}
                    disabled={actions_disabled}
                  />
                </div>
              </div>
            )}
          </Row>
          <Row>{nodesCronStatus}</Row>
        </div>
      }
      body={content}
    />
  );
};

class Timestamp extends Component {
  constructor(props) {
    super(props);
    this.state = { isOpen: false, ...this.props, isNodeAgentStatusModalOpen: false };
  }

  render() {
    const { timestamp, index, universeDetails } = this.props;
    const { isNodeAgentStatusModalOpen } = this.state;
    const defaultExpanded = this.state.isOpen || index === 0;

    const nodeDetails = universeDetails.nodeDetailsSet;
    const nodeIPs = nodeDetails?.map((nodeDetail) => {
      return nodeDetail.cloudInfo.private_ip;
    });

    return (
      <Panel
        eventKey={this.props.eventKey}
        defaultExpanded={defaultExpanded}
        className="health-container"
      >
        <Panel.Heading>
          <Panel.Title
            tag="h4"
            toggle
            onClick={() => {
              this.setState({ isOpen: !this.state.isOpen });
            }}
          >
            <span>{timestampFormatter(timestamp.timestampMoment)}</span>
            <div className="universe-status">
              {countFormatter(timestamp.errorNodes, 'node', 'nodes', true, false, 'failing')}
              {isNonEmptyArray(timestamp.errorNodes) && (
                <>
                  <img src={Wrench} alt="wrench" />
                  <span
                    className="node-agent-status"
                    onClick={() => {
                      this.setState({ isNodeAgentStatusModalOpen: true });
                    }}
                  >
                    Check Node Agent Status
                  </span>
                </>
              )}
              {countFormatter(timestamp.healthyNodes, 'node', 'nodes', false, false, 'healthy')}
              {countFormatter(timestamp.warningNodes, 'node', 'nodes', false, true, 'warning')}
            </div>
          </Panel.Title>
        </Panel.Heading>
        <Panel.Body collapsible>
          <>
            {isNodeAgentStatusModalOpen && (
              <NodeAgentStatusModal
                nodeIPs={nodeIPs}
                onClose={() => this.setState({ isNodeAgentStatusModalOpen: false })}
                open={isNodeAgentStatusModalOpen}
                isAssignedNodes={true}
              />
            )}
            <NodeList nodes={timestamp.nodes} defaultExpanded={defaultExpanded} />
          </>
        </Panel.Body>
      </Panel>
    );
  }
}

const NodeList = (props) => {
  const { nodes, defaultExpanded } = props;
  return (
    <div>
      {nodes.map((node) => (
        <TreeNode
          key={node.ipAddress}
          defaultExpanded={defaultExpanded}
          header={
            <span>
              <span className="tree-node-main-heading">{node.ipAddress}</span>-
              {node.passingChecks.length > 0 &&
                countFormatter(node.passingChecks, 'check', 'checks', false, false, 'OK')}
              {node.failedChecks.length > 0 &&
                countFormatter(node.failedChecks, 'check', 'checks', true, false, 'failed')}
              {node.warningChecks.length > 0 &&
                countFormatter(node.warningChecks, 'check', 'checks', false, true, 'warning')}
            </span>
          }
          body={<ChecksTable checks={node.checks} />}
        />
      ))}
    </div>
  );
};

const ChecksTable = (props) => {
  const { checks } = props;
  return (
    <YBPanelItem
      body={
        <BootstrapTable data={checks}>
          <TableHeaderColumn dataField="key" isKey={true} hidden={true} />
          <TableHeaderColumn
            dataField="message"
            dataFormat={messageFormatter}
            columnClassName="no-border name-column"
            className="no-border name-column"
          >
            Check Type
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="process"
            columnClassName="name-column"
            className="no-border name-column"
          >
            Process
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="details"
            dataFormat={detailsFormatter}
            className="expand-cell"
            columnClassName="word-wrap expand-cell"
          >
            Details
          </TableHeaderColumn>
        </BootstrapTable>
      }
    />
  );
};

const timestampFormatter = (timestampMoment) => (
  <span
    title={timestampMoment
      .fromNow()
      .replace(/^an? /, '1 ')
      .replace(/^1 few/, 'a few')}
  >
    {timestampMoment.calendar()}
  </span>
);

const countFormatter = (items, singleUnit, pluralUnit, hasError, hasWarning, descriptor = '') => {
  if (items.length === 0) {
    return;
  }
  return (
    <span className={`count status-${hasError ? 'bad' : hasWarning ? 'warning' : 'good'}`}>
      <i className={`fa fa-${hasError ? 'times' : hasWarning ? 'exclamation' : 'check'}`} />
      {items.length} {descriptor} {items.length === 1 ? singleUnit : pluralUnit}
    </span>
  );
};

const messageFormatter = (cell, row) => (
  <span>
    {row.has_error && <span className="label label-danger">Failed</span>}
    {row.has_warning && <span className="label label-warning">Warning</span>}
    {row.message}
  </span>
);

const detailsFormatter = (cell, row) => {
  switch (row.details.length) {
    case 0:
      return 'Ok';
    case 1:
      return <pre>{row.details}</pre>;
    default:
      return <pre style={{ whiteSpace: 'pre', overflowY: 'auto' }}>{row.details.join('\n')}</pre>;
  }
};

// For performance optimization, move this to a Redux reducer, so that it doesn't get run on each render.
function prepareData(data, timezone) {
  return data.map((timeData) => {
    let timestampMoment = moment.utc(timeData.timestamp_iso).local();
    if (timezone) {
      timestampMoment = moment.utc(timeData.timestamp_iso).tz(timezone);
    }
    const nodesByIpAddress = {};
    timeData.data.forEach((check) => {
      check.key = getKeyForCheck(check);
      const ipAddress = check.node;
      if (!nodesByIpAddress[ipAddress]) {
        nodesByIpAddress[ipAddress] = {
          ipAddress,
          checks: [],
          passingChecks: [],
          failedChecks: [],
          warningChecks: [],
          hasError: false,
          hasWarning: false
        };
      }
      const node = nodesByIpAddress[ipAddress];
      node.checks.push(check);
      node[
        check.has_error ? 'failedChecks' : check.has_warning ? 'warningChecks' : 'passingChecks'
      ].push(check);
      if (check.has_error) {
        node.hasError = true;
      }
      if (check.has_warning) {
        node.hasWarning = true;
      }
    });
    values(nodesByIpAddress).forEach((node) => {
      node.checks = sortBy(node.checks, (check) =>
        check.has_error ? 0 : check.has_warning ? 0 : 1
      );
    });
    const nodes = sortBy(
      values(nodesByIpAddress),
      (node) => `${node.hasError ? 0 : node.has_warning ? 0 : 1}-${node.ipAddress}`
    );
    const healthyNodes = [];
    const errorNodes = [];
    const warningNodes = [];
    nodes.forEach((node) => {
      (node.hasError ? errorNodes : node.hasWarning ? warningNodes : healthyNodes).push(node);
    });
    return { timestampMoment, nodes, healthyNodes, errorNodes, warningNodes };
  });
}

const getNodesWithInactiveCrons = (universe) => {
  const nodes = [];
  universe.universeDetails.nodeDetailsSet.forEach(function (nodeDetails) {
    if (!nodeDetails.cronsActive) {
      nodes.push(nodeDetails.nodeName);
    }
  });
  return nodes;
};

const getKeyForCheck = (check) => `${check.node}-${check.process}-${check.message}`;
