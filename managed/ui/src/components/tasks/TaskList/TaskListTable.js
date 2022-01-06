// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Link } from 'react-router';
import { toast } from 'react-toastify';
import { YBPanelItem } from '../../panels';
import { timeFormatter, successStringFormatter } from '../../../utils/TableFormatters';
import { YBConfirmModal } from '../../modals';

import './TasksList.scss';

export default class TaskListTable extends Component {
  static defaultProps = {
    title: 'Tasks'
  };
  static propTypes = {
    taskList: PropTypes.array.isRequired,
    overrideContent: PropTypes.object,
    isCommunityEdition: PropTypes.bool
  };

  render() {
    const {
      taskList,
      title,
      overrideContent,
      isCommunityEdition,
      visibleModal,
      hideTaskAbortModal,
      showTaskAbortModal
    } = this.props;

    function nameFormatter(cell, row) {
      return <span>{row.title.replace(/.*:\s*/, '')}</span>;
    }

    function typeFormatter(cell, row) {
      return (
        <span>
          {row.typeName} {row.target}
        </span>
      );
    }

    const abortTaskClicked = (taskUUID) => {
      this.props.abortCurrentTask(taskUUID).then((response) => {
        const taskResponse = response?.payload?.response;
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
      } else if (row.type === 'UpgradeSoftware' && row.details != null) {
        return (
          <span>
            <code>{row.details.ybPrevSoftwareVersion}</code>
            {' => '}
            <code>{row.details.ybSoftwareVersion}</code>
          </span>
        );
      } else if (row.status === 'Running') {
        return (
          <>
            <YBConfirmModal
              name="confirmAbortTask"
              title="Confirm Abort"
              hideConfirmModal={hideTaskAbortModal}
              currentModal={'confirmAbortTask'}
              visibleModal={visibleModal}
              onConfirm={() => abortTaskClicked(row.id)}
              confirmLabel="Abort"
              cancelLabel="Cancel"
            >
              Are you sure you want to abort the task?
            </YBConfirmModal>
            <div className="task-abort-view yb-pending-color" onClick={showTaskAbortModal}>
              <i className="fa fa-chevron-right"></i>
              Abort Task
            </div>
          </>
        );
      } else {
        return <span />;
      }
    };
    const tableBodyContainer = { marginBottom: '1%', paddingBottom: '1%' };
    return (
      <YBPanelItem
        header={<h2 className="task-list-header content-title">{title}</h2>}
        body={
          isCommunityEdition ? (
            overrideContent
          ) : (
            <BootstrapTable
              data={taskList}
              bodyStyle={tableBodyContainer}
              pagination={true}
              search
              multiColumnSearch
              searchPlaceholder="Search by Name or Type"
            >
              <TableHeaderColumn dataField="id" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="type"
                dataFormat={typeFormatter}
                columnClassName="no-border name-column"
                className="no-border"
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
                Name
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
              <TableHeaderColumn dataField="id" dataFormat={taskDetailLinkFormatter} dataSort>
                Notes
              </TableHeaderColumn>
            </BootstrapTable>
          )
        }
      />
    );
  }
}
