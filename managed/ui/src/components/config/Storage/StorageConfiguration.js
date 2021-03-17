// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Tab, Row, Col } from 'react-bootstrap';
import { withRouter } from 'react-router';
import { SubmissionError, change } from 'redux-form';
import _ from 'lodash';
import { YBTabsPanel } from '../../panels';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import AwsStorageConfiguration from './AwsStorageConfiguration';
import { BackupList } from './BackupList';
import { EditBackupList } from './EditBackupList';
import { CreateBackup } from './CreateBackup';
import { storageConfigTypes } from './ConfigType';
import { ConfigControls } from './ConfigControls';
import awss3Logo from './images/aws-s3.png';
import azureLogo from './images/azure_logo.svg';

const getTabTitle = (configName) => {
  switch (configName) {
    case 'S3':
      return <img src={awss3Logo} alt="AWS S3" className="aws-logo" />;
    case 'GCS':
      return (
        <h3>
          <i className="fa fa-database"></i>GCS
        </h3>
      );
    case 'AZ':
      return <img src={azureLogo} alt="Azure" className="azure-logo" />;
    default:
      return (
        <h3>
          <i className="fa fa-database"></i>NFS
        </h3>
      );
  }
};

class StorageConfiguration extends Component {
  constructor(props) {
    super(props);

    this.state = {
      editView: {
        s3: {
          isEdited: false,
          data: {}
        },
        nfs: {
          isEdited: false,
          data: {}
        },
        gcs: {
          isEdited: false,
          data: {}
        },
        az: {
          isEdited: false,
          data: {}
        }
      },
      iamRoleEnabled: false,
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
        {!this.state.listView[configNameFormatted] &&
          <Row className="config-section-header" key={configNameFormatted}>
            <Col lg={8}>{configFields}</Col>
          </Row>
        }
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
    const type =
      (props.activeTab && props.activeTab.toUpperCase())
      || Object.keys(storageConfigTypes)[0];
    Object.keys(values).forEach((key) => {
      if (typeof values[key] === 'string' || values[key] instanceof String)
        values[key] = values[key].trim();
    });
    let dataPayload = { ...values };
    let configName = "";

    // These conditions will pick only the required JSON keys from the respective tab.
    switch (props.activeTab) {
      case 'nfs':
        configName = dataPayload['NFS_CONFIGURATION_NAME'];
        dataPayload['BACKUP_LOCATION'] = dataPayload['NFS_BACKUP_LOCATION'];
        dataPayload = _.pick(dataPayload, ['BACKUP_LOCATION']);
        break;

      case 'gcs':
        configName = dataPayload['GCS_CONFIGURATION_NAME'];
        dataPayload['BACKUP_LOCATION'] = dataPayload['GCS_BACKUP_LOCATION'];
        dataPayload = _.pick(dataPayload, [
          'BACKUP_LOCATION',
          'GCS_CREDENTIALS_JSON'
        ]);
        break;

      case 'az':
        configName = dataPayload['AZ_CONFIGURATION_NAME'];
        dataPayload['BACKUP_LOCATION'] = dataPayload['AZ_BACKUP_LOCATION'];
        dataPayload = _.pick(dataPayload, [
          'BACKUP_LOCATION',
          'AZURE_STORAGE_SAS_TOKEN'
        ]);
        break;

      default:
        if (values['IAM_INSTANCE_PROFILE']) {
          configName = dataPayload['S3_CONFIGURATION_NAME'];
          dataPayload['IAM_INSTANCE_PROFILE'] = dataPayload['IAM_INSTANCE_PROFILE'].toString();
          dataPayload['BACKUP_LOCATION'] = dataPayload['S3_BACKUP_LOCATION'];
          dataPayload = _.pick(dataPayload, [
            'BACKUP_LOCATION',
            'AWS_HOST_BASE',
            'IAM_INSTANCE_PROFILE'
          ]);
        } else {
          configName = dataPayload['S3_CONFIGURATION_NAME'];
          dataPayload['BACKUP_LOCATION'] = dataPayload['S3_BACKUP_LOCATION'];
          dataPayload = _.pick(dataPayload, [
            'AWS_ACCESS_KEY_ID',
            'AWS_SECRET_ACCESS_KEY',
            'BACKUP_LOCATION',
            'AWS_HOST_BASE'
          ]);
        }
        break;
    }

    if (values.type === "edit") {
      this.setState({
        editView: {
          ...this.state.editView,
          [props.activeTab]: {
            isEdited: false,
            data: {}
          }
        },
        listView: {
          ...this.state.listView,
          [props.activeTab]: true
        }
      });

      return (
        this.props
          .editCustomerConfig({
            type: 'STORAGE',
            name: type,
            configName: configName,
            data: dataPayload,
            configUUID: values.configUUID
          }).then(() => {
            if (getPromiseState(this.props.editConfig).isSuccess()) {
              this.props.reset();
              this.props.fetchCustomerConfigs();
            } else if (getPromiseState(this.props.editConfig).isError()) {
              throw new SubmissionError(this.props.editConfig.error);
            }
          })
      )
    } else {
      this.setState({
        listView: {
          ...this.state.listView,
          [props.activeTab]: true
        }
      });

      return (
        this.props
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
            } else if (getPromiseState(this.props.addConfig).isError()) {
              // show server-side validation errors under form inputs
              throw new SubmissionError(this.props.addConfig.error);
            }
          })
      );
    }
  };

  /**
   * This method is used to remove the backup storage config.
   * 
   * @param {string} configUUID Unique id for respective backup config.
   */
  deleteStorageConfig = (configUUID) => {
    this.props.deleteCustomerConfig(configUUID)
      .then(() => {
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
    const data = {
      ...row.data,
      "type": "edit",
      "configUUID": row.configUUID,
      "inUse": row.inUse,
      [`${tab}_BACKUP_LOCATION`]: row.data.BACKUP_LOCATION,
      [`${tab}_CONFIGURATION_NAME`]: row.configName,
    };

    Object.keys(data).map((fieldName) => {
      const fieldValue = data[fieldName];
      this.props.dispatch(change("storageConfigForm", fieldName, fieldValue));
    });

    this.setState({
      editView: {
        ...this.state.editView,
        [activeTab]: {
          isEdited: true,
          data: data
        }
      },
      iamRoleEnabled: data["IAM_INSTANCE_PROFILE"],
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
    this.props.reset();
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
    this.props.reset();
    this.setState({
      editView: {
        ...this.state.editView,
        [activeTab]: {
          isEdited: false,
          data: {}
        }
      },
      iamRoleEnabled: false,
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

  render() {
    const {
      handleSubmit,
      submitting,
      addConfig: { loading },
      customerConfigs
    } = this.props;
    const { iamRoleEnabled } = this.state;
    const activeTab = this.props.activeTab
    || Object.keys(storageConfigTypes)[0].toLowerCase();
    const config = this.getConfigByType(activeTab, customerConfigs);
    const backupListData = customerConfigs.data.filter((list) => {
      if (activeTab === list.name.toLowerCase()) {
        return list;
      }
    });

    if (getPromiseState(customerConfigs).isLoading()) {
      return <YBLoading />;
    }

    if (
      getPromiseState(customerConfigs).isSuccess() ||
      getPromiseState(customerConfigs).isEmpty()
    ) {
      const configs = [
        <Tab
          eventKey={'s3'}
          title={getTabTitle('S3')}
          key={'s3-tab'}
          unmountOnExit={true}
        >
          {!this.state.listView.s3 &&
            <AwsStorageConfiguration
              data={this.state.editView.s3.data}
              iamRoleEnabled={iamRoleEnabled}
              iamInstanceToggle={this.iamInstanceToggle}
            />
          }
        </Tab>
      ];
      Object.keys(storageConfigTypes).forEach((configName) => {
        if (this.state.editView[activeTab]?.isEdited) {
          const configFields = [];
          const configTemplate = storageConfigTypes[configName];
          configTemplate.fields.forEach((field) => {
            const value = this.state.editView[activeTab].data;
            configFields.push(
              <EditBackupList
                key={field.id}
                configName={configName}
                data={value}
                field={field}
              />
            );
          });
          configs.push(this.wrapFields(configFields, configName));
        } else {
          const configFields = [];
          const config = storageConfigTypes[configName];
          config.fields.forEach((field) => {
            configFields.push(
              <CreateBackup
                key={field.id}
                configName={configName}
                field={field}
              />
            );
          });
          configs.push(this.wrapFields(configFields, configName));
        }
      });

      return (
        <div className="provider-config-container">
          <form name="storageConfigForm" onSubmit={handleSubmit(this.addStorageConfig)}>
            <YBTabsPanel
              defaultTab={Object.keys(storageConfigTypes)[0].toLowerCase()}
              activeTab={activeTab}
              id="storage-config-tab-panel"
              className="config-tabs"
              routePrefix="/config/backup/"
            >
              {this.state.listView[activeTab] &&
                <BackupList
                  {...this.props}
                  activeTab={activeTab}
                  data={backupListData}
                  onCreateBackup={() => this.createBackupConfig(activeTab)}
                  onEditConfig={(row) => this.editBackupConfig(row, activeTab)}
                  deleteStorageConfig={(row) => this.deleteStorageConfig(row)}
                />
              }

              {configs}
            </YBTabsPanel>

            <ConfigControls
              {...this.state}
              activeTab={activeTab}
              showListView={() => this.showListView(activeTab)}
            />
          </form>
        </div>
      );
    }
    return <YBLoading />;
  }
}

export default withRouter(StorageConfiguration);
