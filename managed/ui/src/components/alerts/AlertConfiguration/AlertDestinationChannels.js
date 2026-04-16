import React, { Suspense, useState } from 'react';
import { Col, DropdownButton, MenuItem, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useDispatch } from 'react-redux';
import {
  GET_ALERT_CONFIGS,
  deleteAlertChannel,
  getAlertChannels,
  updateAlertChannel
} from '../../../actions/customers';
import { YBButton } from '../../common/forms/fields';
import { YBConfirmModal } from '../../modals';
import { YBPanelItem } from '../../panels';
import { AddDestinationChannelForm } from './AddDestinationChannelForm';
import { isNonAvailable } from '../../../utils/LayoutUtils';
import clsx from 'clsx';

import { toast } from 'react-toastify';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { useMount } from 'react-use';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { startCase, toLower } from 'lodash';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import './AlertDestinationChannels.scss';

const Composer = React.lazy(() => import('../../../redesign/features/alerts/TemplateComposer/Composer'));

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
      initialValues['webhookURLSlack'] = values.params.webhookUrl;
      initialValues['username'] = values.name;
      break;
    case 'Email':
      initialValues['defaultRecipients'] = values.params.defaultRecipients;

      if (!values.params.defaultRecipients) {
        initialValues['emailIds'] = values.params.recipients.join(',');
      }

      initialValues['defaultSmtp'] = values.params.defaultSmtpSettings;

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
      initialValues['sendResolved'] = values.params.sendResolved;
      if (!values.params.httpAuth?.type) {
        break;
      }
      initialValues['webhookAuthType'] = startCase(toLower(values.params.httpAuth.type));
      if (values.params.httpAuth?.type === 'BASIC') {
        const { username, password } = values.params.httpAuth;
        initialValues['webhookBasicUsername'] = username;
        initialValues['webhookBasicPassword'] = password;
      }
      if (values.params.httpAuth?.type === 'TOKEN') {
        const { tokenHeader, tokenValue } = values.params.httpAuth;
        initialValues['webhookTokenHeader'] = tokenHeader;
        initialValues['webhookTokenValue'] = tokenValue;
      }
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
    return params.recipients?.join(',');
  }
  return params.webhookUrl;
};

export const AlertDestinationChannels = (props) => {
  const dispatch = useDispatch();
  const [alertChannels, setAlertChannels] = useState([]);
  const [showModal, setShowModal] = useState(false);
  const [type, setType] = useState('');
  const [initialValues, setInitialValues] = useState({});
  const [showCustomTemplateEditor, setShowCustomTemplateEditor] = useState(false);

  const {
    customer, featureFlags
  } = props;
  const isReadOnly = isNonAvailable(
    customer.data.features, 'alert.channels.actions');

  const enableNotificationTemplates = featureFlags.test.enableNotificationTemplates
    || featureFlags.released.enableNotificationTemplates;

  const enableCustomEmailTemplates = featureFlags.test.enableCustomEmailTemplates
    || featureFlags.released.enableCustomEmailTemplates;

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
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.MODIFY_ALERT_DESTINATION}
            isControl
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
          </RbacValidator>

          {!isReadOnly && (
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.DELETE_ALERT_DESTINATION}
              isControl
              overrideStyle={{ display: 'block' }}
            >
              <MenuItem
                onClick={() => {
                  deleteChannel(row);
                }}
              >
                <i className="fa fa-trash"></i> Delete Channel
              </MenuItem>
            </RbacValidator>
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
    <div className={clsx('alert-destination-channels', { 'minPadding': showCustomTemplateEditor })}>
      {
        !showCustomTemplateEditor && (
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.GET_ALERT_CONFIGURATIONS}
          >
            <>
              <div className="alert-destinations">
                <div>
                  A notification channel defines the means by which an alert is sent (ex: Email) as well as
                  who should receive the notification.
                </div>
                {!isReadOnly && (
                  <div>
                    {enableCustomEmailTemplates && (
                      <RbacValidator
                        accessRequiredOn={ApiPermissionMap.CREATE_ALERT_CHANNEL_TEMPLATE}
                        isControl
                      >
                        <YBButton
                          btnText="Customize Notification Templates"
                          btnClass="btn btn-default cutomize_notification_but"
                          onClick={() => {
                            setShowModal(false);
                            setShowCustomTemplateEditor(true);
                          }}
                          data-testid="customize_notification_template"
                        />
                      </RbacValidator>
                    )}
                    <RbacValidator
                      accessRequiredOn={ApiPermissionMap.CREATE_ALERT_CONFIGURATIONS}
                      isControl
                    >
                      <YBButton
                        btnText="Add Channel"
                        btnClass="btn btn-orange"
                        onClick={() => {
                          setType('create');
                          setShowModal(true);
                        }}
                      />
                    </RbacValidator>
                  </div>
                )}
              </div>
              <Row>
                <Col xs={12} lg={12} className="noLeftPadding">
                  <b>{alertChannels?.length} Notification Channels</b>
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
                          Notification Type
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
              {showModal && !showCustomTemplateEditor && (
                <AddDestinationChannelForm
                  visible={showModal}
                  onHide={() => {
                    setShowModal(false);
                    setInitialValues({});
                  }}
                  defaultChannel={initialValues.CHANNEL_TYPE ?? 'email'}
                  defaultRecipients={initialValues.defaultRecipients}
                  defaultSmtp={initialValues.defaultSmtp}
                  type={type}
                  editAlertChannel={editAlertChannel}
                  editValues={type === 'edit' ? initialValues : {sendResolved: true}}
                  updateDestinationChannel={getAlertChannelsList}
                  enableNotificationTemplates={enableNotificationTemplates}
                  {...props}
                />
              )}
            </>
          </RbacValidator>
        )
      }

      {
        showCustomTemplateEditor && !showModal && (
          <Suspense fallback={<YBLoadingCircleIcon />}>
            <Composer onHide={() => {
              setShowCustomTemplateEditor(false);
            }} />
          </Suspense>
        )
      }
    </div>
  );
};
