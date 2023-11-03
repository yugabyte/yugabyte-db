// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold all the destination list of alerts.

import { useEffect, useState } from 'react';
import { Button, DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBConfirmModal } from '../../modals';
import { YBPanelItem } from '../../panels';
import { AlertDestinationDetails } from './AlertDestinationDetails';
import { isNonAvailable } from '../../../utils/LayoutUtils';

import { RbacValidator, hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacApiPermValidator';

import './AlertDestinationConfiguration.scss';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
/**
 * This is the header for YB Panel Item.
 */
const header = (isReadOnly, destinationCount, onAddAlertDestination) => (
  <>
    <FlexContainer>
      <FlexShrink>
        A destination consist of one or more{' '}
        <a className="channel-link" href="/admin/alertConfig/notificationChannels" target="_self">
          channels
        </a>
        <br />
        Whenever an alert is triggered, it sends the related data to its designated destination.
      </FlexShrink>
      <FlexShrink className="pull-right">
        {!isReadOnly && (
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.CREATE_ALERT_DESTINATION}
            isControl
          >
            <Button bsClass="btn btn-orange btn-config" onClick={() => onAddAlertDestination(true)}>
              Add Destination
            </Button>
          </RbacValidator>
        )}
      </FlexShrink>
    </FlexContainer>
    <h5 className="table-container-title alert-dest-count pull-left">{`${destinationCount} Alert Destinations`}</h5>
  </>
);

export const AlertDestinations = (props) => {
  const [alertDestination, setAlertDestination] = useState([]);
  const [alertDestinationDetails, setAlertDestinationDetails] = useState({});
  const {
    customer,
    alertDestinations,
    closeModal,
    deleteAlertDestination,
    modal: { showModal, visibleModal },
    onAddAlertDestination,
    setInitialValues,
    showDeleteModal,
    showDetailsModal,
    getAlertChannels
  } = props;
  const [options, setOptions] = useState({
    noDataText: 'Loading...',
    sortName: 'name',
    sortOrder: 'asc'
  });
  const isReadOnly = isNonAvailable(customer.data.features, 'alert.destinations.actions');

  const setRsponseObject = () => {
    const result = new Map();

    alertDestinations().then((destinations) => {
      getAlertChannels().then((channels) => {
        destinations?.forEach((dest) => {
          result.set(dest.uuid, {
            name: dest.name,
            uuid: dest.uuid,
            defaultDestination: dest.defaultDestination,
            channels: []
          });

          dest['channels'].forEach((rx) => {
            const matchedRx = channels.find((channel) => channel.uuid === rx);
            const destination = result.get(dest.uuid);

            destination.channels.push({
              uuid: rx,
              channelType: matchedRx.params.channelType,
              channelName: matchedRx.name,
              webHookURL: matchedRx.params?.webhookUrl,
              recipients: matchedRx.params?.recipients,
              params: matchedRx.params
            });
            result.set(dest.uuid, destination);
          });
        });
        setOptions({ noDataText: 'There is no data to display ' });
        setAlertDestination(Array.from(result.values()));
      });
    });
  };

  const onInit = () => setRsponseObject();
  useEffect(onInit, []);

  /**
   * This method is used to set and pass the destination details to
   * the alert destination modal.
   *
   * @param {object} row Respective row
   */
  const setModalDetails = (row) => {
    const data = row.channels.map((channel) => {
      return {
        channelType: channel.channelType,
        channelName: channel.channelName,
        webHookURL: channel?.webHookURL,
        recipients: channel?.recipients,
        params: channel?.params
      };
    });

    setAlertDestinationDetails(data);
  };

  /**
   * This method will help us to delete the respective row record.
   *
   * @param {object} row Respective row data.
   */
  const onDeleteDestination = (row) => {
    deleteAlertDestination(row.uuid).then(() => {
      setRsponseObject();
    });
  };

  /**
   * This method will help us to edit the respective alert destination record.
   *
   * @param {object} row Respective row data.
   */
  const onEditDestination = (row) => {
    const channels = row.channels.map((channel) => {
      return {
        value: channel.uuid,
        label: channel.channelName
      };
    });

    const initialVal = {
      type: 'update',
      uuid: row.uuid,
      defaultDestination: row.defaultDestination,
      ALERT_DESTINATION_NAME: row.name,
      DESTINATION_CHANNEL_LIST: channels
    };

    setInitialValues(initialVal);
    onAddAlertDestination(true);
  };

  /**
   *
   * @param {destination} row
   * @returns Comma seperated channel names.
   */
  const getChannelNames = (row) => {
    return row.map((channel) => channel.channelName).join(', ');
  };

  // This method will handle all the required actions for the particular row.

  const editActionLabel = isReadOnly ? 'Destination Details' : 'Edit Destination';
  const canEditDestination = hasNecessaryPerm(ApiPermissionMap.MODIFY_ALERT_DESTINATION);
  const canDeleteDestination = hasNecessaryPerm(ApiPermissionMap.DELETE_ALERT_DESTINATION);
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
              setModalDetails(row);
              showDetailsModal();
            }}
          >
            <i className="fa fa-info-circle"></i> Channels Details
          </MenuItem>
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.MODIFY_ALERT_DESTINATION}
            isControl
          >
            <MenuItem disabled={!canEditDestination} onClick={() => canEditDestination && onEditDestination(row)}>
              <i className="fa fa-pencil"></i> {editActionLabel}
            </MenuItem>
          </RbacValidator>
          {!isReadOnly && (
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.DELETE_ALERT_DESTINATION}
              isControl
              overrideStyle={{ display: 'block' }}
            >
              <MenuItem onClick={() => canDeleteDestination && showDeleteModal(row.name)} disabled={!canDeleteDestination} >
                <i className="fa fa-trash"></i> Delete Destination
              </MenuItem>
            </RbacValidator>
          )}
          {
            <YBConfirmModal
              name="delete-alert-destination"
              title="Confirm Delete"
              onConfirm={() => onDeleteDestination(row)}
              currentModal={row.name}
              visibleModal={visibleModal}
              hideConfirmModal={closeModal}
            >
              Are you sure you want to delete {row.name} Alert Destination?
            </YBConfirmModal>
          }
        </DropdownButton>
      </>
    );
  };

  return (
    <RbacValidator
      accessRequiredOn={ApiPermissionMap.GET_ALERT_DESTINATIONS}
    >
      <>
        <YBPanelItem
          header={header(isReadOnly, alertDestination.length, onAddAlertDestination)}
          body={
            <>
              <BootstrapTable
                className="backup-list-table middle-aligned-table"
                data={alertDestination}
                options={options}
                pagination
              >
                <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
                <TableHeaderColumn
                  dataField="name"
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Destinations
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="channels"
                  dataFormat={getChannelNames}
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Channels
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="defaultDestination"
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Default Destination
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

        <AlertDestinationDetails
          visible={showModal && visibleModal === 'alertDestinationDetailsModal'}
          onHide={closeModal}
          details={alertDestinationDetails}
        />
      </>
    </RbacValidator>
  );
};
