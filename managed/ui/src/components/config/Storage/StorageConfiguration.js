// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Tab, Row, Col } from 'react-bootstrap';
import { withRouter } from 'react-router';
import { SubmissionError, change } from 'redux-form';
import _ from 'lodash';
import { YBTabsPanel } from '../../panels';
import { YBButton } from '../../common/forms/fields';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import AwsStorageConfiguration from './AwsStorageConfiguration';
import { BackupList } from './BackupList';
import { EditBackupList } from './EditBackupList';
import { CreateBackup } from './CreateBackup';
import { StorageConfigTypes } from './ConfigType';
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

  addStorageConfig = (values, action, props) => {
    const type =
      (props.activeTab && props.activeTab.toUpperCase())
      || Object.keys(StorageConfigTypes)[0];
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
          dataPayload['BACKUP_LOCATION'] = dataPayload['AWS_BACKUP_LOCATION'];
          dataPayload = _.pick(dataPayload, [
            'BACKUP_LOCATION',
            'AWS_HOST_BASE',
            'IAM_INSTANCE_PROFILE'
          ]);
        } else {
          configName = dataPayload['S3_CONFIGURATION_NAME'];
          dataPayload['BACKUP_LOCATION'] = dataPayload['AWS_BACKUP_LOCATION'];
          dataPayload = _.pick(dataPayload, [
            'AWS_ACCESS_KEY_ID',
            'AWS_SECRET_ACCESS_KEY',
            'BACKUP_LOCATION',
            'AWS_HOST_BASE'
          ]);
        }
        break;
    }

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
      }), this.setState({
        listView: {
          ...this.state.listView,
          [props.activeTab]: true
        }
      })
    );
  };

  deleteStorageConfig = (configUUID) => {
    this.props.deleteCustomerConfig(configUUID)
      .then(() => {
        this.props.reset(); // reset form to initial values
        this.props.fetchCustomerConfigs();
      });
  };

  // This method will enable the edit config form.
  editBackupConfig = (row, activeTab) => {
    const tab = activeTab.toUpperCase();
    const data = {
      ...row.data,
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

  // This method will enable the create backup form.
  createBackupConfig = (activeTab) => {
    this.props.reset();
    this.setState({
      listView: {
        ...this.state.listView,
        [activeTab]: false
      }
    });
  };

  // This method will enable the backup list view.
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

  // This method will disbale the access key and secret key
  // field if IAM role is enabled.
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
    || Object.keys(StorageConfigTypes)[0].toLowerCase();
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
      Object.keys(StorageConfigTypes).forEach((configName) => {
        if (this.state.editView[activeTab]?.isEdited) {
          const configFields = [];
          const configTemplate = StorageConfigTypes[configName];
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
          const config = StorageConfigTypes[configName];
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
              defaultTab={Object.keys(StorageConfigTypes)[0].toLowerCase()}
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
