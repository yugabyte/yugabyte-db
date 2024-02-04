// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Tab, Row, Col } from 'react-bootstrap';
import { connect } from 'react-redux';
import { withRouter } from 'react-router';
import { SubmissionError } from 'redux-form';
import _ from 'lodash';
import { YBTabsPanel } from '../../panels';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import AwsStorageConfiguration from './AwsStorageConfiguration';
import GcsStorageConfiguration from './GcsStorageConfiguration';
import { BackupList } from './BackupList';
import { BackupConfigField } from './BackupConfigField';
import { storageConfigTypes } from './ConfigType';
import { ConfigControls } from './ConfigControls';
import awss3Logo from './images/aws-s3.png';
import azureLogo from './images/azure_logo.svg';
import gcsLogo from './images/gcs-logo.png';
import nfsIcon from './images/nfs.svg';
import { Formik } from 'formik';

const getTabTitle = (configName) => {
  switch (configName) {
    case 'S3':
      return (
        <span>
          <img src={awss3Logo} alt="AWS S3" className="s3-logo" /> Amazon S3
        </span>
      );
    case 'GCS':
      return (
        <span>
          <img src={gcsLogo} alt="Google Cloud Storage" className="gcs-logo" /> Google Cloud Storage
        </span>
      );
    case 'AZ':
      return (
        <span>
          <img src={azureLogo} alt="Azure" className="azure-logo" /> Azure Storage
        </span>
      );
    default:
      return (
        <span>
          <img src={nfsIcon} alt="NFS" className="nfs-icon" /> Network File System
        </span>
      );
  }
};

class StorageConfiguration extends Component {
  constructor(props) {
    super(props);
    this.state = {
      editView: {
        s3: false,
        nfs: false,
        gcs: false,
        az: false
      },
      iamRoleEnabled: false,
      useGcpIam: false,
      listView: {
        s3: true,
        nfs: true,
        gcs: true,
        az: true
      }
    };
  }

  componentDidMount() {
    this.props.fetchCustomerConfigs();
  }

  getConfigByType = (name, customerConfigs) => {
    return customerConfigs.data.find((config) => config.name.toLowerCase() === name);
  };

  wrapFields = (configFields, configName) => {
    const configNameFormatted = configName.toLowerCase();

    return (
      <Tab
        eventKey={configNameFormatted}
        title={getTabTitle(configName)}
        key={configNameFormatted + '-tab'}
        unmountOnExit={true}
      >
        {!this.state.listView[configNameFormatted] && (
          <Row className="config-section-header" key={configNameFormatted}>
            <Col lg={8}>{configFields}</Col>
          </Row>
        )}
      </Tab>
    );
  };

  /**
   * This method will handle the edit as well as the add action
   * for the respective backup configuration. It will also setup
   * the datapayload accrodingly and update the state based on the
   * config type.
   *
   * @param {any} values Input values.
   * @param action no-use for now.
   * @param {props} props is used to maintain the repsective tab actions.
   * @returns
   */
  addStorageConfig = (values, action, props) => {
    const type = props.activeTab?.toUpperCase() || Object.keys(storageConfigTypes)[0];
    Object.keys(values).forEach((key) => {
      if (typeof values[key] === 'string' || values[key] instanceof String)
        values[key] = values[key].trim();
    });
    let dataPayload = { ...values };
    let configName = '';

    // These conditions will pick only the required JSON keys from the respective tab.
    switch (props.activeTab) {
      case 'nfs': {
        configName = dataPayload['NFS_CONFIGURATION_NAME'];
        dataPayload['BACKUP_LOCATION'] = dataPayload['NFS_BACKUP_LOCATION'];
        dataPayload = _.pick(dataPayload, ['BACKUP_LOCATION']);
        break;
      }

      case 'gcs': {
        let FIELDS;
        if (values['USE_GCP_IAM']) {
            configName = dataPayload['GCS_CONFIGURATION_NAME'];
            dataPayload['BACKUP_LOCATION'] = dataPayload['GCS_BACKUP_LOCATION'];
            dataPayload['USE_GCP_IAM'] = dataPayload['USE_GCP_IAM'].toString();
            FIELDS = ['BACKUP_LOCATION', 'USE_GCP_IAM'];
        } else {
            configName = dataPayload['GCS_CONFIGURATION_NAME'];
            dataPayload['BACKUP_LOCATION'] = dataPayload['GCS_BACKUP_LOCATION'];
            FIELDS = ['BACKUP_LOCATION', 'GCS_CREDENTIALS_JSON'];
        }
        dataPayload = _.pick(dataPayload, FIELDS);
        break;
      }

      case 'az': {
        configName = dataPayload['AZ_CONFIGURATION_NAME'];
        dataPayload['BACKUP_LOCATION'] = dataPayload['AZ_BACKUP_LOCATION'];
        dataPayload = _.pick(dataPayload, ['BACKUP_LOCATION', 'AZURE_STORAGE_SAS_TOKEN']);
        break;
      }

      default: {
        let FIELDS;
        if (values['IAM_INSTANCE_PROFILE']) {
          configName = dataPayload['S3_CONFIGURATION_NAME'];
          dataPayload['IAM_INSTANCE_PROFILE'] = dataPayload['IAM_INSTANCE_PROFILE'].toString();
          dataPayload['BACKUP_LOCATION'] = dataPayload['S3_BACKUP_LOCATION'];
          FIELDS = ['BACKUP_LOCATION', 'AWS_HOST_BASE', 'IAM_INSTANCE_PROFILE'];
        } else {
          configName = dataPayload['S3_CONFIGURATION_NAME'];
          dataPayload['BACKUP_LOCATION'] = dataPayload['S3_BACKUP_LOCATION'];
          FIELDS = [
            'AWS_ACCESS_KEY_ID',
            'AWS_SECRET_ACCESS_KEY',
            'BACKUP_LOCATION',
            'AWS_HOST_BASE',
            'PATH_STYLE_ACCESS'
          ];
        }
        if (dataPayload?.PROXY_SETTINGS?.PROXY_HOST) FIELDS.push('PROXY_SETTINGS.PROXY_HOST');
        if (dataPayload?.PROXY_SETTINGS?.PROXY_PORT) FIELDS.push('PROXY_SETTINGS.PROXY_PORT');
        if (dataPayload?.PROXY_SETTINGS?.PROXY_USERNAME) {
          FIELDS.push('PROXY_SETTINGS.PROXY_USERNAME');
          if (dataPayload?.PROXY_SETTINGS?.PROXY_PASSWORD)
            FIELDS.push('PROXY_SETTINGS.PROXY_PASSWORD');
        }
        dataPayload = _.pick(dataPayload, FIELDS);

        break;
      }
    }

    if (values.type === 'update') {
      return this.props
        .editCustomerConfig({
          type: 'STORAGE',
          name: type,
          configName: configName,
          data: dataPayload,
          configUUID: values.configUUID
        })
        .then(() => {
          if (getPromiseState(this.props.editConfig).isSuccess()) {
            this.props.reset();
            this.props.fetchCustomerConfigs();
            this.setState({
              editView: {
                ...this.state.editView,
                [props.activeTab]: false
              },
              listView: {
                ...this.state.listView,
                [props.activeTab]: true
              }
            });
          } else if (getPromiseState(this.props.editConfig).isError()) {
            throw new SubmissionError(this.props.editConfig.error);
          }
        });
    } else {
      return this.props
        .addCustomerConfig({
          type: 'STORAGE',
          name: type,
          configName: configName,
          data: dataPayload
        })
        .then((resp) => {
          if (getPromiseState(this.props.addConfig).isSuccess()) {
            // reset form after successful submission due to BACKUP_LOCATION value is shared across all tabs
            this.props.reset();
            this.props.fetchCustomerConfigs();

            // Change to list view if form is successfully submitted.
            this.setState({
              ...this.state,
              listView: {
                [props.activeTab]: true
              }
            });
          } else if (getPromiseState(this.props.addConfig).isError()) {
            // show server-side validation errors under form inputs
            throw new SubmissionError(this.props.addConfig.error);
          }
        });
    }
  };

  /**
   * This method is used to remove the backup storage config.
   *
   * @param {string} configUUID Unique id for respective backup config.
   */
  deleteStorageConfig = (configUUID) => {
    this.props.deleteCustomerConfig(configUUID).then(() => {
      this.props.reset(); // reset form to initial values
      this.props.fetchCustomerConfigs();
    });
  };

  /**
   * This method is used to update the backup config details and setup
   * the initial state accordingly. We're also setting up the data
   * object which will help us to setup the payload.
   *
   * @param {object} row It's a respective row details for any config.
   * @param {string} activeTab It's a respective active tab.
   */
  editBackupConfig = (row, activeTab) => {
    const tab = activeTab.toUpperCase();
    let initialVal = {};
    switch (activeTab) {
      case 'nfs':
        initialVal = {
          type: 'update',
          configUUID: row?.configUUID,
          [`${tab}_BACKUP_LOCATION`]: row.data?.BACKUP_LOCATION,
          [`${tab}_CONFIGURATION_NAME`]: row?.configName
        };
        break;

      case 'gcs':
        initialVal = {
          type: 'update',
          configUUID: row?.configUUID,
          [`${tab}_BACKUP_LOCATION`]: row.data?.BACKUP_LOCATION,
          [`${tab}_CONFIGURATION_NAME`]: row?.configName,
          USE_GCP_IAM: row.data?.USE_GCP_IAM,
          GCS_CREDENTIALS_JSON: row.data?.GCS_CREDENTIALS_JSON
        };
        break;

      case 'az':
        initialVal = {
          type: 'update',
          configUUID: row?.configUUID,
          [`${tab}_BACKUP_LOCATION`]: row.data?.BACKUP_LOCATION,
          [`${tab}_CONFIGURATION_NAME`]: row?.configName,
          AZURE_STORAGE_SAS_TOKEN: row.data?.AZURE_STORAGE_SAS_TOKEN
        };
        break;

      default:
        initialVal = {
          type: 'update',
          configUUID: row?.configUUID,
          IAM_INSTANCE_PROFILE: row.data?.IAM_INSTANCE_PROFILE,
          AWS_ACCESS_KEY_ID: row.data?.AWS_ACCESS_KEY_ID || '',
          AWS_SECRET_ACCESS_KEY: row.data?.AWS_SECRET_ACCESS_KEY || '',
          [`${tab}_BACKUP_LOCATION`]: row.data?.BACKUP_LOCATION,
          [`${tab}_CONFIGURATION_NAME`]: row?.configName,
          AWS_HOST_BASE: row.data?.AWS_HOST_BASE
        };
        if (row?.data?.PATH_STYLE_ACCESS)
          initialVal['PATH_STYLE_ACCESS'] = row?.data?.PATH_STYLE_ACCESS;
        if (row?.data?.PROXY_SETTINGS?.PROXY_HOST)
          initialVal['PROXY_SETTINGS'] = row?.data?.PROXY_SETTINGS;
        break;
    }
    this.props.setInitialValues(initialVal);
    this.setState({
      editView: {
        ...this.state.editView,
        [activeTab]: true
      },
      iamRoleEnabled: row.data['IAM_INSTANCE_PROFILE'] || false,
      useGcpIam: row.data['USE_GCP_IAM'] || false,
      listView: {
        ...this.state.listView,
        [activeTab]: false
      }
    });
  };

  /**
   * This method will enable the create backup config form.
   *
   * @param {string} activeTab It's a respective active tab.
   */
  createBackupConfig = (activeTab) => {
    if (this.props.enablePathStyleAccess)
      this.props.setInitialValues({ PATH_STYLE_ACCESS: 'true' });
    this.setState({
      listView: {
        ...this.state.listView,
        [activeTab]: false
      }
    });
  };

  /**
   * This method will enable the list view of backup storage config.
   *
   * @param {string} activeTab It's a respective active tab.
   */
  showListView = (activeTab) => {
    this.props.setInitialValues();
    this.setState({
      editView: {
        ...this.state.editView,
        [activeTab]: false
      },
      iamRoleEnabled: false,
      useGcpIam: false,
      listView: {
        ...this.state.listView,
        [activeTab]: true
      }
    });
  };

  /**
   * This method will disbale the access key and secret key
   * field if IAM role is enabled.
   *
   * @param {event} event Toggle input value.
   */
  iamInstanceToggle = (event) => {
    this.setState({ iamRoleEnabled: event.target.checked });
  };

  gcpIamToggle = (event) => {
    this.setState({ useGcpIam: event.target.checked });
  };

  render() {
    const {
      handleSubmit,
      customerConfigs,
      initialValues,
      enablePathStyleAccess,
      enableS3BackupProxy
    } = this.props;
    const { iamRoleEnabled, useGcpIam, editView, listView } = this.state;
    const activeTab = this.props.activeTab || Object.keys(storageConfigTypes)[0].toLowerCase();
    const backupListData = customerConfigs.data.filter((list) => {
      if (activeTab === list.name.toLowerCase()) {
        return list;
      }
      return null;
    });

    if (getPromiseState(customerConfigs).isLoading()) {
      return <YBLoading />;
    }

    if (
      getPromiseState(customerConfigs).isSuccess() ||
      getPromiseState(customerConfigs).isEmpty()
    ) {
      const configs = [
        <Tab eventKey={'s3'} title={getTabTitle('S3')} key={'s3-tab'} unmountOnExit={true}>
          {!listView.s3 && (
            <AwsStorageConfiguration
              iamRoleEnabled={iamRoleEnabled}
              iamInstanceToggle={this.iamInstanceToggle}
              isEdited={editView[activeTab]}
              enablePathStyleAccess={enablePathStyleAccess}
              enableS3BackupProxy={enableS3BackupProxy}
            />
          )}
        </Tab>,
         <Tab eventKey={'gcs'} title={getTabTitle('GCS')} key={'gcs-tab'} unmountOnExit={true}>
         {!listView.gcs && (
           <GcsStorageConfiguration
             useGcpIam={useGcpIam}
             gcpIamToggle={this.gcpIamToggle}
             isEdited={editView[activeTab]}
           />
         )}
       </Tab>
      ];

      Object.keys(storageConfigTypes).forEach((configName) => {
        const configFields = [];
        const config = storageConfigTypes[configName];
        config.fields.forEach((field) => {
          configFields.push(
            <BackupConfigField
              key={field.id}
              configName={configName}
              field={field}
              isEdited={editView[activeTab]}
            />
          );
        });
        configs.push(this.wrapFields(configFields, configName));
      });

      return (
        <div className="provider-config-container">
          <Formik initialValues={initialValues}>
            <form name="storageConfigForm" onSubmit={handleSubmit(this.addStorageConfig)}>
              <YBTabsPanel
                defaultTab={Object.keys(storageConfigTypes)[0].toLowerCase()}
                activeTab={activeTab}
                id="storage-config-tab-panel"
                className="config-tabs"
                routePrefix="/config/backup/"
              >
                {this.state.listView[activeTab] && (
                  <BackupList
                    {...this.props}
                    activeTab={activeTab}
                    data={backupListData}
                    onCreateBackup={() => this.createBackupConfig(activeTab)}
                    onEditConfig={(row) => this.editBackupConfig(row, activeTab)}
                    deleteStorageConfig={(row) => this.deleteStorageConfig(row)}
                  />
                )}

                {configs}

                {!listView[activeTab] && (
                  <ConfigControls
                    {...this.state}
                    activeTab={activeTab}
                    showListView={() => this.showListView(activeTab)}
                  />
                )}
              </YBTabsPanel>
            </form>
          </Formik>
        </div>
      );
    }
    return <YBLoading />;
  }
}

function mapStateToProps(state) {
  const {
    featureFlags: { test, released }
  } = state;

  return {
    enablePathStyleAccess: test.enablePathStyleAccess || released.enablePathStyleAccess,
    enableS3BackupProxy: test.enableS3BackupProxy || released.enableS3BackupProxy
  };
}

export default connect(mapStateToProps, null)(withRouter(StorageConfiguration));
