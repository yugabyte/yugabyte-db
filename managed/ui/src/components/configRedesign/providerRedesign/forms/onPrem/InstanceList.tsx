/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useSelector } from 'react-redux';

import { YBLoadingCircleIcon } from '../../../../common/indicators';
import { getPromiseState } from '../../../../../utils/PromiseUtils';
import { isNonEmptyArray } from '../../../../../utils/ObjectUtils';

import styles from './InstanceList.module.scss';

interface InstanceListProps {
  disabled?: boolean;
  isError?: boolean;
}

const PRECHECK_STATUS_ORDER = [
  'None',
  'Initializing',
  'Created',
  'Running',
  'Success',
  'Failure',
  'Aborted'
];

export const InstanceList = ({ disabled, isError }: InstanceListProps) => {
  const { nodeInstanceList } = useSelector((state: any) => state.cloud);
  const { customerTaskList } = useSelector((state: any) => state.tasks);
  let nodeListItems = [];
  if (getPromiseState(nodeInstanceList).isSuccess()) {
    //finding prechecking tasks
    const lastTasks = new Map();
    if (isNonEmptyArray(customerTaskList)) {
      customerTaskList
        .filter((task: any) => task.type === 'PrecheckNode')
        .sort((a: any, b: any) => b.createTime.localeCompare(a.createTime))
        .forEach((task: any) => {
          if (!lastTasks.has(task.targetUUID)) {
            lastTasks.set(task.targetUUID, task);
          }
        });
    }
    const taskStatusOrder = (task: any) => {
      const status = task ? task.status : 'None';
      return PRECHECK_STATUS_ORDER.indexOf(status);
    };
    nodeListItems = nodeInstanceList.data.map((item: any) => {
      return {
        nodeId: item.nodeUuid,
        nodeName: item.nodeName,
        inUse: item.inUse,
        ip: item.details.ip,
        instanceType: item.details.instanceType,
        region: item.details.region,
        zone: item.details.zone,
        zoneUuid: item.zoneUuid,
        instanceName: item.instanceName,
        precheckTask: lastTasks.get(item.nodeUuid),
        precheckStatus: taskStatusOrder(lastTasks.get(item.nodeUuid))
      };
    });
  }
  const renderIconByStatus = (task: any) => {
    if (!task) {
      return <div />;
    }
    const status = task.status;
    const errorIcon = (
      <i
        className="fa fa-warning yb-fail-color precheck-status-container"
        onClick={() => {
          window.open(`/tasks/${task.id}`);
        }}
      />
    );
    if (status === 'Created' || status === 'Initializing') {
      return <i className="fa fa-clock-o" />;
    } else if (status === 'Success') {
      return <i className="fa fa-check-circle yb-success-color" />;
    } else if (status === 'Running') {
      return <YBLoadingCircleIcon size="inline" />;
    } else if (status === 'Failure' || status === 'Aborted') {
      return errorIcon;
    }
    return errorIcon;
  };

  const formatPreflightCheck = (_: any, row: any) => {
    return <div>{row?.inUse ? '' : renderIconByStatus(row?.precheckTask)}</div>;
  };
  return (
    <div className={styles.bootstrapTableContainer}>
      <BootstrapTable
        data={nodeListItems}
        search
        multiColumnSearch
        options={{
          clearSearch: true
        }}
        containerClass="onprem-nodes-table"
      >
        <TableHeaderColumn dataField="nodeId" isKey={true} hidden={true} dataSort />
        <TableHeaderColumn dataField="instanceName" dataSort>
          Identifier
        </TableHeaderColumn>
        <TableHeaderColumn dataField="ip" dataSort>
          Address
        </TableHeaderColumn>
        <TableHeaderColumn dataField="precheckStatus" dataFormat={formatPreflightCheck} dataSort>
          Preflight Check
        </TableHeaderColumn>
        <TableHeaderColumn dataField="region" dataSort>
          Region
        </TableHeaderColumn>
        <TableHeaderColumn dataField="zone" dataSort>
          Zone
        </TableHeaderColumn>
        <TableHeaderColumn dataField="instanceType" dataSort>
          Instance Type
        </TableHeaderColumn>
      </BootstrapTable>
    </div>
  );
};
