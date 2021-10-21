// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold all the configuration list of alerts.

import moment from 'moment';
import React, { useEffect, useState } from 'react';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBLoading } from '../../common/indicators';
import { YBConfirmModal } from '../../modals';
import { YBPanelItem } from '../../panels';

/**
 * This is the header for YB Panel Item.
 */
const header = (
  alertsCount,
  onCreateAlert,
  enablePlatformAlert,
  handleMetricsCall,
  setInitialValues
) => (
  <>
    <h5 className="table-container-title pull-left">{`${alertsCount} Alert Configurations`}</h5>
    <FlexContainer className="pull-right">
      <FlexShrink>
        <DropdownButton
          className="alert-config-actions btn btn-orange"
          title="Create Alert Config"
          id="bg-nested-dropdown"
          bsStyle="danger"
          pullRight
        >
          <MenuItem
            className="alert-config-list"
            onClick={() => {
              handleMetricsCall('UNIVERSE');
              onCreateAlert(true);
              enablePlatformAlert(false);
              setInitialValues({ ALERT_TARGET_TYPE: 'allUniverses' });
            }}
          >
            <i className="fa fa-globe"></i> Universe Alert
          </MenuItem>

          <MenuItem
            className="alert-config-list"
            onClick={() => {
              handleMetricsCall('PLATFORM');
              onCreateAlert(true);
              enablePlatformAlert(true);
              setInitialValues({ ALERT_TARGET_TYPE: 'allUniverses' });
            }}
          >
            <i className="fa fa-clone tab-logo" aria-hidden="true"></i> Platform Alert
          </MenuItem>
        </DropdownButton>
      </FlexShrink>
    </FlexContainer>
  </>
);

/**
 * Request payload to get the list of alerts.
 */
const payload = {
  uuids: [],
  name: null,
  active: true,
  targetUuid: null,
  routeUuid: null
};

export const AlertsList = (props) => {
  const [alertList, setAlertList] = useState([]);
  const [alertDestinationList, setAlertDestinationList] = useState([]);
  const [defaultDestination, setDefaultDestination] = useState([]);
  const {
    alertConfigs,
    alertUniverseList,
    alertDestinations,
    closeModal,
    deleteAlertConfig,
    enablePlatformAlert,
    handleMetricsCall,
    modal: { visibleModal },
    onCreateAlert,
    showDeleteModal,
    setInitialValues,
    universes
  } = props;
  const [options, setOptions] = useState({
    noDataText: 'Loading...',
    sortName: 'createTime',
    sortOrder: 'desc'
  });

  const onInit = () => {
    alertConfigs(payload).then((res) => {
      setAlertList(res);
      setOptions({ noDataText: 'There is no data to display ' });
    });

    alertDestinations().then((res) => {
      setDefaultDestination(res.find((destination) => destination.defaultDestination));
      setAlertDestinationList(res);
    });
  };

  useEffect(onInit, []);

  /**
   * This method is used to congifure the destinationUUID with its repsective
   * destination name.
   *
   * @param {string} cell Not in-use.
   * @param {object} row Respective details
   */
  const formatRoutes = (cell, row) => {
    if (row.defaultDestination) {
      return (
        <span className="text-red text-regular"> Use Default ({defaultDestination.name})</span>
      );
    }
    const route = alertDestinationList
      .map((destination) => {
        return destination.uuid === row.destinationUUID ? destination.name : null;
      })
      .filter((res) => res !== null);

    if (route.length > 0) {
      return route;
    }
    return <span className="text-red text-regular"> No destination</span>;
  };

  /**
   * This method is used to get the severity from thresholds object.
   *
   * @param {string} cell Not in-use.
   * @param {object} row Respective details.
   */
  const formatThresholds = (cell, row) => {
    return Object.keys(row.thresholds)
      .map((threshold) => threshold)
      .join(', ');
  };

  /**
   * This method will return the date in format.
   *
   * @param {string} cell Not in-use.
   * @param {object} row Respective row.
   */
  const formatcreatedTime = (cell, row) => {
    return moment(row.createTime).format('MM/DD/yyyy');
  };

  /**
   * This method will help us to edit the details for a respective row.
   *
   * @param {object} row Respective row object.
   */
  const onEditAlertConfig = (row) => {
    row.targetType === 'PLATFORM' ? enablePlatformAlert(true) : enablePlatformAlert(false);

    // setting up ALERT_DESTINATION_LIST.
    const destination = alertDestinationList
      .map((destination) => {
        return destination.uuid === row.destinationUUID
          ? {
              value: destination.uuid,
              label: destination.name
            }
          : null;
      })
      .filter((res) => res !== null);

    // setting up ALERT_METRICS_CONDITION_POLICY.
    const condition = Object.keys(row.thresholds).map((key) => {
      return {
        _SEVERITY: key,
        _CONDITION: row.thresholds[key].condition,
        _THRESHOLD: row.thresholds[key].threshold
      };
    });

    // setting up ALERT_TARGET_TYPE & ALERT_UNIVERSE_LIST.
    const currentDestination = destination[0]?.value
      ? destination[0]?.value
      : row.defaultDestination
      ? '<default>'
      : '<empty>';
    const targetType = row.target.all ? 'allUniverses' : 'selectedUniverses';
    const univerList =
      isNonEmptyArray(row.target.uuids) &&
      row.target.uuids.map((list) => alertUniverseList.find((universe) => universe.value === list));
    // setting up the initial values.
    const initialVal = {
      type: 'update',
      uuid: row.uuid,
      createTime: row.createTime,
      ALERT_CONFIGURATION_NAME: row.name,
      ALERT_CONFIGURATION_DESCRIPTION: row.description,
      ALERT_TARGET_TYPE: targetType,
      ALERT_UNIVERSE_LIST: univerList,
      ALERT_METRICS_CONDITION: row.template,
      ALERT_METRICS_DURATION: row.durationSec,
      ALERT_METRICS_CONDITION_POLICY: condition,
      ALERT_DESTINATION_LIST: currentDestination,
      thresholdUnit: row.thresholdUnit
    };

    setInitialValues(initialVal);
    onCreateAlert(true);
  };

  /**
   * This method will help us to delete the respective row record.
   *
   * @param {object} row Respective row data.
   */
  const onDeleteConfig = (row) => {
    deleteAlertConfig(row.uuid).then(() => {
      alertConfigs(payload).then((res) => {
        setAlertList(res);
      });
    });
  };

  const formatAlertDestinationName = (cell) => {
    if (cell.all) return 'ALL';
    const targetUniverse = cell.uuids
      .map((uuid) => {
        return universes.data.find((destination) => destination.universeUUID === uuid);
      })
      .filter(Boolean); //filtering undefined, if the universe is already deleted

    return (
      <span>
        {targetUniverse.map((u) => (
          <div key={u.universeUUID}>{u.name}</div>
        ))}
      </span>
    );
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
          <MenuItem
            onClick={() => {
              handleMetricsCall(row.targetType);
              onEditAlertConfig(row);
            }}
          >
            <i className="fa fa-pencil"></i> Edit Alert
          </MenuItem>

          <MenuItem onClick={() => showDeleteModal(row?.uuid)}>
            <i className="fa fa-trash"></i> Delete Alert
          </MenuItem>
        </DropdownButton>
        <YBConfirmModal
          name="delete-alert-config"
          title="Confirm Delete"
          onConfirm={() => onDeleteConfig(row)}
          currentModal={row?.uuid}
          visibleModal={visibleModal}
          hideConfirmModal={closeModal}
        >
          Are you sure you want to delete {row?.name} Alert Config?
        </YBConfirmModal>
      </>
    );
  };

  if (!getPromiseState(universes).isSuccess()) {
    return <YBLoading />;
  }

  return (
    <YBPanelItem
      header={header(
        alertList.length,
        onCreateAlert,
        enablePlatformAlert,
        handleMetricsCall,
        setInitialValues
      )}
      body={
        <>
          <BootstrapTable
            className="backup-list-table middle-aligned-table"
            data={alertList}
            options={options}
            pagination
          >
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
              dataSort
              columnClassName="no-border name-column"
              className="no-border"
            >
              Type
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="target"
              columnClassName="no-border name-column"
              className="no-border"
              dataFormat={formatAlertDestinationName}
            >
              Target Universes
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="thresholds"
              dataFormat={formatThresholds}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Severity
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="destinationUUID"
              dataFormat={formatRoutes}
              columnClassName="no-border name-column"
              className="no-border"
            >
              Destination
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="createTime"
              dataFormat={formatcreatedTime}
              dataSort
              width="120px"
              columnClassName="no-border name-column"
              className="no-border"
            >
              Created
            </TableHeaderColumn>
            <TableHeaderColumn
              dataField="template"
              columnClassName="no-border name-column"
              className="no-border"
            >
              Metric Name
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
