// Copyright (c) YugabyteDB, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Link } from 'react-router';
import { toast } from 'react-toastify';
import { YBPanelItem } from '../../panels';
import { timeFormatter, successStringFormatter } from '../../../utils/TableFormatters';
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { SoftwareUpgradeTaskType } from '../../universes/helpers/universeHelpers';
import { YBConfirmModal } from '../../modals';
import './TasksList.scss';

export default class TaskListTable extends Component {
  static defaultProps = {
    title: 'Tasks'
  };
  static propTypes = {
    taskList: PropTypes.array.isRequired,
    overrideContent: PropTypes.object
  };

  state = {
    selectedTaskUUID: undefined
  }

  render() {
    const { taskList, title, visibleModal, hideTaskAbortModal, showTaskAbortModal, featureFlags, showTaskDrawer } = this.props;
    const isNewTaskDetailsUIEnabled = featureFlags?.test?.newTaskDetailsUI || featureFlags?.released?.newTaskDetailsUI;
    const self = this;
    function nameFormatter(cell, row) {
      return <span>{row.title.replace(/.*:\s*/, '')}</span>;
    }

    function typeFormatter(cell, row) {
      return row.correlationId && hasNecessaryPerm(ApiPermissionMap.GET_LOGS) && !isNewTaskDetailsUIEnabled ? (
        <Link to={`/logs/?queryRegex=${row.correlationId}&startDate=${row.createTime}`}>
          {row.typeName} {row.target}
        </Link>
      ) : (
        <span style={{ paddingLeft: '5px' }}>{`${row.typeName} ${row.target}`}</span>
      );
    }

    const abortTaskClicked = (taskUUID) => {
      this.props.abortTask(taskUUID).then((response) => {
        const taskResponse = response?.payload?.response;
        // eslint-disable-next-line no-empty
        if (taskResponse && (taskResponse.status === 200 || taskResponse.status === 201)) {
        } else {
          const toastMessage = taskResponse?.data?.error
            ? taskResponse?.data?.error
            : taskResponse?.statusText;
          toast.error(toastMessage);
        }
      });
    };

    const taskDetailLinkFormatter = function (cell, row) {
      if (row.status === 'Failure' || row.status === 'Aborted') {
        return <Link to={`/tasks/${row.id}`}>See Details</Link>;
        // eslint-disable-next-line eqeqeq
      } else if (row.type === 'UpgradeSoftware' && row.details !== null) {
        return (
          <span>
            <code>{row?.details?.versionNumbers?.ybPrevSoftwareVersion}</code>
            {' => '}
            <code>{row?.details?.versionNumbers?.ybSoftwareVersion}</code>
          </span>
        );
      } else if ((row.status === 'Running' || row.status === 'Created') && row.abortable) {
        return (
          <>
            <RbacValidator
              accessRequiredOn={{ ...ApiPermissionMap.ABORT_TASK, onResource: row.targetUUID }}
              isControl
            >
              <div className="task-abort-view yb-pending-color" onClick={() => {
                self.setState({ selectedTaskUUID: row.id });
                showTaskAbortModal();
              }}>
                Abort Task
              </div>
            </RbacValidator>
          </>
        );
      } else if (
        [
          SoftwareUpgradeTaskType.SOFTWARE_UPGRADE,
          SoftwareUpgradeTaskType.ROLLBACK_UPGRADE,
          SoftwareUpgradeTaskType.FINALIZE_UPGRADE
        ].includes(row.type)
      ) {
        return <Link to={`/tasks/${row.id}`}>See Details</Link>;
      } else {
        return <span />;
      }
    };


    const tableBodyContainer = { marginBottom: '1%', paddingBottom: '1%' };
    return (
      <RbacValidator
        accessRequiredOn={ApiPermissionMap.GET_TASKS_LIST}
      >
        <YBPanelItem
          header={<h2 className="task-list-header content-title">{title}</h2>}
          body={
            <BootstrapTable
              data={taskList}
              bodyStyle={tableBodyContainer}
              pagination={true}
              search
              multiColumnSearch
              searchPlaceholder="Search by Name or Type"
              hover={isNewTaskDetailsUIEnabled}
              options={{
                onRowClick: (task) => {
                  if (isNewTaskDetailsUIEnabled) {
                    showTaskDrawer(task.id);
                  }
                }
              }
              }
              trStyle={isNewTaskDetailsUIEnabled && { cursor: 'pointer' }}
            >
              <TableHeaderColumn dataField="id" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="type"
                dataFormat={typeFormatter}
                columnClassName="no-border name-column"
                className={`no-border ${isNewTaskDetailsUIEnabled ? 'task-type-column' : ''}`}
              >
                Type
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="title"
                dataFormat={nameFormatter}
                dataSort
                columnClassName="no-border name-column"
                className="no-border"
              >
                {isNewTaskDetailsUIEnabled ? 'Performed On' : 'Name'}
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="percentComplete"
                dataSort
                columnClassName="no-border name-column"
                className="no-border"
                dataFormat={successStringFormatter}
              >
                Status
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="userEmail"
                dataSort
                columnClassName="no-border name-column"
                className="no-border"
              >
                User
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="createTime"
                dataFormat={timeFormatter}
                dataSort
                columnClassName="no-border "
                className="no-border"
                dataAlign="left"
              >
                Start Time
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="completionTime"
                dataFormat={timeFormatter}
                dataSort
                columnClassName="no-border name-column"
                className="no-border"
              >
                End Time
              </TableHeaderColumn>
              {
                !isNewTaskDetailsUIEnabled && (
                  <TableHeaderColumn dataField="id" dataFormat={taskDetailLinkFormatter} dataSort>
                    Notes
                  </TableHeaderColumn>
                )
              }
            </BootstrapTable>
          }
        />
        <YBConfirmModal
          name="confirmAbortTask"
          title="Confirm Abort"
          hideConfirmModal={hideTaskAbortModal}
          currentModal={'confirmAbortTask'}
          visibleModal={visibleModal}
          onConfirm={() => abortTaskClicked(this.state.selectedTaskUUID)}
          confirmLabel="Abort"
          cancelLabel="Cancel"
        >
          Are you sure you want to abort the task?
        </YBConfirmModal>
      </RbacValidator>
    );
  }
}
