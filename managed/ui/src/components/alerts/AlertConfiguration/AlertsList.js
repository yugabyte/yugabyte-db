// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold all the configuration list of alerts.

import React, { useEffect, useState } from 'react';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBConfirmModal } from '../../modals';
import { YBPanelItem } from '../../panels';

/**
 * This is the header for YB Panel Item.
 */
const header = (onCreateAlert, enablePlatformAlert, handleMetricsCall) => (
  <>
    <h2 className="table-container-title pull-left">Alert Configurations</h2>
    <FlexContainer className="pull-right">
      <FlexShrink>
        <DropdownButton
          className="alert-config-actions btn btn-default"
          title="Create Alert Config"
          id="bg-nested-dropdown"
          pullRight
        >
          <MenuItem
            className="alert-config-list"
            onClick={() => {
              handleMetricsCall('UNIVERSE');
              onCreateAlert(true);
              enablePlatformAlert(false);
            }}
          >
            <i className="fa fa-globe"></i> Universe Alert
          </MenuItem>

          <MenuItem
            className="alert-config-list"
            onClick={() => {
              handleMetricsCall('CUSTOMER');
              onCreateAlert(true);
              enablePlatformAlert(true);
            }}
          >
            <i className="fa fa-clone tab-logo" aria-hidden="true"></i> Platform Alert
          </MenuItem>
        </DropdownButton>
      </FlexShrink>
    </FlexContainer>
  </>
);

export const AlertsList = (props) => {
  const [alertList, setAlertList] = useState([]);
  const {
    alertConfigs,
    closeModal,
    deleteAlertConfig,
    enablePlatformAlert,
    handleMetricsCall,
    modal: { visibleModal },
    onCreateAlert,
    showDeleteModal
  } = props;

  useEffect(() => {
    const body = {
      uuids: [],
      name: null,
      active: true,
      targetType: 'UNIVERSE',
      template: 'REPLICATION_LAG',
      targetUuid: null,
      routeUuid: null
    };

    alertConfigs(body).then((res) => {
      setAlertList(res);
    });
  }, []);

  /**
   * This method will help us to edit the details for a respective row.
   *
   * @param {object} row Respective row object.
   */
  const onEditAlertConfig = (row) => {
    console.log(row, '******* row on edit..');

    // const initialVal = {};

    // setInitialValues(initialVal);
    // onCreateAlert(true);
  };

  /**
   * This method will help us to delete the respective row record.
   *
   * @param {object} row Respective row data.
   */
  const onDeleteConfig = (row) => {
    deleteAlertConfig(row.uuid).then(() => {
      alertConfigs().then((res) => {
        setAlertList(res);
      });
    });
  };

  // This method will handle all the required actions for the particular row.
  const formatConfigActions = (cell, row) => {
    return (
      <>
        <DropdownButton
          className="backup-config-actions btn btn-default"
          title="Actions"
          id="bg-nested-dropdown"
          pullRight
        >
          <MenuItem onClick={() => onEditAlertConfig(row)}>
            <i className="fa fa-pencil"></i> Edit Alert
          </MenuItem>

          <MenuItem onClick={() => showDeleteModal(row.name)}>
            <i className="fa fa-trash"></i> Delete Alert
          </MenuItem>

          {
            <YBConfirmModal
              name="delete-alert-config"
              title="Confirm Delete"
              onConfirm={() => onDeleteConfig(row)}
              currentModal={row.name}
              visibleModal={visibleModal}
              hideConfirmModal={closeModal}
            >
              Are you sure you want to delete {row.name} Alert Config?
            </YBConfirmModal>
          }
        </DropdownButton>
      </>
    );
  };

  // TODO: This needs to be updated once the real data will come.
  // For now, we're dealing with the mock data.
  return (
    <YBPanelItem
      header={header(onCreateAlert, enablePlatformAlert, handleMetricsCall)}
      body={
        <>
          <BootstrapTable className="backup-list-table middle-aligned-table" data={alertList}>
            <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
            <TableHeaderColumn
              dataField="name"
              columnClassName="no-border name-column"
              className="no-border"
            >
              Name
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="targetType"
              // dataFormat={}
              dataSort
              columnClassName="no-border name-column"
              className="no-border"
            >
              Type
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="thresholds"
              // dataFormat={}
              dataSort
              columnClassName="no-border name-column"
              className="no-border"
            >
              Severity
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="routeUUID"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Destinations
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="createTime"
              // dataFormat={}
              dataSort
              columnClassName="no-border name-column"
              className="no-border"
            >
              Created
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="template"
              // dataFormat={}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Metric Name
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="createdBy"
              // dataFormat={}
              dataSort
              columnClassName="no-border name-column"
              className="no-border"
            >
              Created By
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="configActions"
              dataFormat={(cell, row) => formatConfigActions(cell, row)}
              columnClassName="yb-actions-cell"
              className="yb-actions-cell"
            >
              Actions
            </TableHeaderColumn>
          </BootstrapTable>
        </>
      }
      noBackground
    />
  );
};
