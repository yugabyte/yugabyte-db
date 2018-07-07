// Copyright (c) YugaByte, Inc.

import React from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import * as moment from 'moment';
import { sortBy, values } from 'lodash';

import { YBLoading } from 'components/common/indicators';
import TreeNode from 'components/common/TreeNode';
import { YBPanelItem } from 'components/panels';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import { getPromiseState } from 'utils/PromiseUtils';

import './UniverseHealthCheckList.scss';

const UniverseHealthCheckList = props => {
  const {universe: {healthCheck}} = props;
  let content = <span/>;
  if (getPromiseState(healthCheck).isLoading()) {
    content = <YBLoading />;
  } else if (getPromiseState(healthCheck).isSuccess() && isNonEmptyArray(healthCheck.data)) {
    const data = [...healthCheck.data].reverse();
    const timestamps = prepareData(data);
    content = (
      <div>
        <h2 className="health-check-header content-title">Health Checks</h2>
        <TimestampList timestamps={timestamps} />
      </div>
    );
  }

  return (
    <div className="universe-detail-content-container UniverseHealthCheckList">
      {content}
    </div>
  );
};

const TimestampList = props => {
  const {timestamps} = props;
  return (
    <div>
      {timestamps.map(timestamp => (
        <TreeNode
          key={timestamp.timestampMoment.unix()}
          header={
            <div>
              <span className="tree-node-main-heading">{timestampFormatter(timestamp.timestampMoment)}</span>
              {countFormatter(timestamp.healthyNodes, 'node', 'nodes', false, 'healthy')}
              {countFormatter(timestamp.errorNodes, 'node', 'nodes', true, 'failing')}
            </div>
          }
          body={
            <div>
              <NodeList nodes={timestamp.nodes} />
            </div>
          }
        />
      ))}
    </div>
  );
};

const NodeList = props => {
  const {nodes} = props;
  return (
    <div>
      {nodes.map(node => (
        <TreeNode
          key={node.ipAddress}
          header={
            <div>
              <span className="tree-node-main-heading">{node.ipAddress}</span>
              {countFormatter(node.passingChecks, 'check', 'checks', false, 'OK')}
              {countFormatter(node.failedChecks, 'check', 'checks', true, 'failed')}
            </div>
          }
          body={<ChecksTable checks={node.checks} />}
        />
      ))}
    </div>
  );
};

const ChecksTable = props => {
  const {checks} = props;
  return (
    <YBPanelItem
      body={
        <BootstrapTable data={checks}>
          <TableHeaderColumn dataField="key" isKey={true} hidden={true}/>
          <TableHeaderColumn dataField="has_error" dataFormat={checkStatusFormatter}>
            Status
          </TableHeaderColumn>
          <TableHeaderColumn dataField="process">
            Process
          </TableHeaderColumn>
          <TableHeaderColumn dataField="message"
              columnClassName="no-border name-column" className="no-border">
            Check Type
          </TableHeaderColumn>
          <TableHeaderColumn dataField="details" dataFormat={detailsFormatter}
              className="expand-cell" columnClassName="word-wrap expand-cell">
            Details
          </TableHeaderColumn>
        </BootstrapTable>
      }
    />
  );
};

const timestampFormatter = timestampMoment => (
  <span title={timestampMoment.fromNow().replace(/^an? /, '1 ').replace(/^1 few/, 'a few')}>
    {timestampMoment.calendar()}
  </span>
);

const countFormatter = (items, singleUnit, pluralUnit, hasError, descriptor = '') => {
  if (items.length === 0) {
    return <span/>;
  }
  return (
    <span className={`count status-${hasError ? 'bad' : 'good'}`}>
      <i className={`fa fa-${hasError ? 'times' : 'check'}`} />
      {items.length} {descriptor} {items.length === 1 ? singleUnit : pluralUnit}
    </span>
  );
};

const checkStatusFormatter = hasError => (
  <span className={`status status-${hasError ? 'bad' : 'good'}`}>
    <i className={`fa fa-${hasError ? 'times' : 'check'}`} />
    {hasError ? 'Error' : 'OK'}
  </span>
);

const detailsFormatter = (cell, row) => (
  <span>
    {row.has_error && (
      <span className="label label-danger">ERROR</span>
    )}
    {row.details}
  </span>
);

// For performance optimization, move this to a Redux reducer, so that it doesn't get run on each render.
function prepareData(data) {
  return data.map(timeDataJson => {
    const timeData = JSON.parse(timeDataJson);
    const timestampMoment = moment(timeData.timestamp);
    const nodesByIpAddress = {};
    timeData.data.forEach(check => {
      check.key = getKeyForCheck(check);
      const ipAddress = check.node;
      if (!nodesByIpAddress[ipAddress]) {
        nodesByIpAddress[ipAddress] = {
          ipAddress,
          checks: [],
          passingChecks: [],
          failedChecks: [],
          hasError: false,
        };
      }
      const node = nodesByIpAddress[ipAddress];
      node.checks.push(check);
      node[check.has_error ? 'failedChecks' : 'passingChecks'].push(check);
      if (check.has_error) {
        node.hasError = true;
      }
    });
    values(nodesByIpAddress).forEach(node => {
      node.checks = sortBy(node.checks, check => check.has_error ? 0 : 1);
    });
    const nodes = sortBy(values(nodesByIpAddress), node => `${node.hasError ? 0 : 1}-${node.ipAddress}`);
    const healthyNodes = [];
    const errorNodes = [];
    nodes.forEach(node => {
      (node.hasError ? errorNodes : healthyNodes).push(node);
    });
    return {timestampMoment, nodes, healthyNodes, errorNodes};
  });
}

const getKeyForCheck = check => `${check.node}-${check.process}-${check.message}`;

export default UniverseHealthCheckList;
