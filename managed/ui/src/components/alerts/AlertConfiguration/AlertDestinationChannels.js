import React, { useState } from 'react';
import { Col, DropdownButton, MenuItem, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useDispatch } from 'react-redux';
import {
  deleteAlertChannel,
  getAlertChannels,
  updateAlertChannel
} from '../../../actions/customers';
import { YBButton } from '../../common/forms/fields';
import { YBConfirmModal } from '../../modals';
import { YBPanelItem } from '../../panels';
import { AddDestinationChannelForm } from './AddDestinationChannelForm';
import { isNonAvailable } from '../../../utils/LayoutUtils';

import './AlertDestinationChannels.scss';
import { toast } from 'react-toastify';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { useMount } from 'react-use';

const prepareInitialValues = (values) => {
  const initialValues = {
    CHANNEL_TYPE: values.params.channelType.toLowerCase(),
    uuid: values.uuid,
    slack_name: values.name,
    email_name: values.name,
    pagerDuty_name: values.name,
    webHook_name: values.name,
    notificationTitle: values.params.titleTemplate,
    notificationText: values.params.textTemplate
  };

  switch (values.params.channelType) {
    case 'Slack':
      initialValues['webhookURL'] = values.params.webhookUrl;
      initialValues['username'] = values.name;
      break;
    case 'Email':
      initialValues['defaultRecipients'] = values.params.defaultRecipients;

      if (!values.params.defaultRecipients) {
        initialValues['emailIds'] = values.params.recipients.join(',');
      }

      initialValues['customSmtp'] = !values.params.defaultSmtpSettings;

      if (!values.params.defaultSmtpSettings) {
        initialValues['smtpData'] = values.params.smtpData;
        initialValues['smtpData']['smtpPassword'] = '';
        initialValues['smtpData']['useSSL'] = values.params.smtpData.useSSL || false;
        initialValues['smtpData']['useTLS'] = values.params.smtpData.useTLS || false;
      }
      break;
    case 'PagerDuty':
      initialValues['apiKey'] = values.params.apiKey;
      initialValues['routingKey'] = values.params.routingKey;
      break;
    case 'WebHook':
      initialValues['webhookURL'] = values.params.webhookUrl;
      break;
    default:
      throw new Error(`Unknown Channel type ${values.params.channelType}`);
  }

  return initialValues;
};

const getDestination = (params) => {
  if (params.channelType === 'Email') {
    if (params.defaultRecipients) {
      return '[Default Recipients]';
    }
    return params.recipients && params.recipients.join(',');
  }
  return params.webhookUrl;
};

export const AlertDestinationChannels = (props) => {
  const dispatch = useDispatch();
  const [alertChannels, setAlertChannels] = useState([]);
  const [showModal, setShowModal] = useState(false);
  const [type, setType] = useState('');
  const [initialValues, setInitialValues] = useState({});
  const {
    customer, featureFlags
  } = props;
  const isReadOnly = isNonAvailable(
    customer.data.features, 'alert.channels.actions');

  const enableNotificationTemplates = featureFlags.test.enableNotificationTemplates
    || featureFlags.released.enableNotificationTemplates;

  const getAlertChannelsList = () => {
    dispatch(getAlertChannels()).then((resp) => setAlertChannels(resp.payload.data));
  };

  const deleteChannel = (row) => {
    dispatch(deleteAlertChannel(row.uuid)).then((resp) => {
      if (resp.payload.status === 200) {
        toast.success(`Alert channel "${row.name}" deleted successfully`);
        getAlertChannelsList();
      } else {
        toast.error(createErrorMessage(resp.payload));
      }
    });
  };

  useMount(() => {
    getAlertChannelsList();
  });

  const editAlertChannel = (channelUUID, values) => {
    dispatch(updateAlertChannel(channelUUID, values)).then((resp) => {
      if (resp.payload.status === 200) {
        toast.success(`Alert channel "${values.name}" edited successfully`);
        getAlertChannelsList();
        setShowModal(false);
      } else {
        toast.error(createErrorMessage(resp.payload));
      }
    });
  };

  const editActionLabel = isReadOnly ? "Channel Details" : "Edit Channel";
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
              setInitialValues(prepareInitialValues(row));
              setType('edit');
              setShowModal(true);
            }}
          >
            <i className="fa fa-pencil"></i> {editActionLabel}
          </MenuItem>

          {!isReadOnly && (
          <MenuItem
            onClick={() => {
              deleteChannel(row);
            }}
          >
            <i className="fa fa-trash"></i> Delete Channel
          </MenuItem>
          )}

          {
            <YBConfirmModal
              name="delete-alert-destination"
              title="Confirm Delete"
              currentModal={row.name}
            >
              Are you sure you want to delete {row.name} Alert Destination?
            </YBConfirmModal>
          }
        </DropdownButton>
      </>
    );
  };

  return (
    <>
      <div className="alert-destinations">
        <div>
          A notification channel defines the means by which an alert is sent (ex: Email) as well as
          who should receive the notification.
        </div>
        {!isReadOnly && (
        <div>
          <YBButton
            btnText="Add Channel"
            btnClass="btn btn-orange"
            onClick={() => {
              setType('create');
              setShowModal(true);
            }}
          />
        </div>
        )}
      </div>
      <Row>
        <Col xs={12} lg={12} className="noLeftPadding">
          <b>{alertChannels.length} Notification Channels</b>
        </Col>
      </Row>
      <Row>
        <YBPanelItem
          body={
            <>
              <BootstrapTable
                className="backup-list-table middle-aligned-table"
                data={alertChannels}
                options={{
                  noDataText: 'Loading...'
                }}
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
                  dataField="params"
                  dataFormat={(params) => params.channelType}
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Channel Type
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="params"
                  dataFormat={(params) => getDestination(params)}
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Destination
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
      </Row>
      {showModal && (
        <AddDestinationChannelForm
          visible={showModal}
          onHide={() => {
            setShowModal(false);
            setInitialValues({});
          }}
          defaultChannel={initialValues.CHANNEL_TYPE ?? 'email'}
          defaultRecipients={initialValues.defaultRecipients}
          customSmtp={initialValues.customSmtp}
          type={type}
          editAlertChannel={editAlertChannel}
          editValues={type === 'edit' ? initialValues : {}}
          updateDestinationChannel={getAlertChannelsList}
          enableNotificationTemplates={enableNotificationTemplates}
          {...props}
        />
      )}
    </>
  );
};
