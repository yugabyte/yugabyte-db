// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Tab, Row, Col, Alert } from 'react-bootstrap';
import { YBTabsPanel } from '../../panels';
import { YBTextInput, YBButton } from '../../common/forms/fields';
import { withRouter } from 'react-router';
import { Field } from 'redux-form';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import { YBConfirmModal } from '../../modals';
import { isDefinedNotNull } from "utils/ObjectUtils";
import AwsStorageConfiguration from './AwsStorageConfiguration';

import awss3Logo from './images/aws-s3.png';
import { isNonEmptyObject, isEmptyObject } from '../../../utils/ObjectUtils';

const storageConfigTypes = {
  NFS: {
    title: "NFS Storage",
    fields: [{
      id: "BACKUP_LOCATION",
      label: "NFS Storage Path",
      placeHolder: "NFS Storage Path"
    }]
  },
  GCS: {
    title: "GCS Storage",
    fields: [{
      id: "BACKUP_LOCATION",
      label: "GCS Bucket",
      placeHolder: "GCS Bucket"
    }, {
      id: "GCS_CREDENTIALS_JSON",
      label: "GCS Credentials",
      placeHolder: "GCS Credentials JSON"
    }]
  },
  AZ: {
    title: "Azure Storage",
    fields: [{
      id: "BACKUP_LOCATION",
      label: "Container URL",
      placeHolder: "Container URL"
    }, {
      id: "AZURE_STORAGE_SAS_TOKEN",
      label: "SAS Token",
      placeholder: "SAS Token"
    }]
  }
};

const getTabTitle = (configName) => {
  switch (configName) {
    case "S3":
      return <img src={awss3Logo} alt="AWS S3" className="aws-logo" />;
    case "GCS":
      return <h3><i className="fa fa-database"></i>GCS</h3>;
    default:
      return <h3><i className="fa fa-database"></i>NFS</h3>;
  }
}

class StorageConfiguration extends Component {

  getConfigByType = (name, customerConfigs) => {
    return customerConfigs.data.find(config => config.name.toLowerCase() === name);
  }

  wrapFields = (configFields, configName, configControls) => {
    const configNameFormatted = configName.toLowerCase();
    return (
      <Tab eventKey={configNameFormatted}
        title={ getTabTitle(configName) }
        key={configNameFormatted+"-tab"}
        unmountOnExit={true}
      >
        <Row className="config-section-header" key={configNameFormatted}>
          <Col lg={8}>
            { configFields }
          </Col>
          {configControls &&
            <Col lg={4}>
              { configControls }
            </Col>
          }
        </Row>
      </Tab>
    );
  }

  addStorageConfig = (values, action, props) => {
    const type = (props.activeTab && props.activeTab.toUpperCase()) || Object.keys(storageConfigTypes)[0];
    Object.keys(values).forEach((key) => { if (typeof values[key] === 'string' || values[key] instanceof String) values[key] = values[key].trim(); });
    const dataPayload = { ...values };
    if (props.activeTab === 's3') {
      if (values["IAM_INSTANCE_PROFILE"]) {
        delete dataPayload["AWS_ACCESS_KEY_ID"];
        delete dataPayload["AWS_SECRET_ACCESS_KEY"];
      }
      if ("IAM_INSTANCE_PROFILE" in dataPayload) {
        dataPayload["IAM_INSTANCE_PROFILE"] = dataPayload["IAM_INSTANCE_PROFILE"].toString();
      }
    }
    this.props.addCustomerConfig({
      "type": "STORAGE",
      "name": type,
      "data": dataPayload
    });
  }

  deleteStorageConfig = configUUID => {
    this.props.deleteCustomerConfig(configUUID);
  }

  showDeleteConfirmModal = configName => {
    this.props.showDeleteStorageConfig(configName);
  }

  componentDidMount() {
    this.props.fetchCustomerConfigs();
  }

  componentDidUpdate(prevProps) {
    const { addConfig, deleteConfig } = this.props;
    if (getPromiseState(addConfig).isLoading()) {
      this.props.fetchCustomerConfigs();
    } else if (getPromiseState(deleteConfig).isLoading()) {
      this.props.fetchCustomerConfigs();
    }
  }

  render() {
    const { handleSubmit, submitting, addConfig: { loading, error }, customerConfigs } = this.props;
    if (getPromiseState(customerConfigs).isLoading()) {
      return <YBLoading />;
    }

    if (getPromiseState(customerConfigs).isSuccess() || getPromiseState(customerConfigs).isEmpty()) {
      const configs = [
        <Tab eventKey={"s3"} title={getTabTitle("S3")} key={"s3-tab"} unmountOnExit={true}>
          <AwsStorageConfiguration {...this.props} />
        </Tab>
      ];
      Object.keys(storageConfigTypes).forEach((configName) => {
        const config = customerConfigs.data.find(config => config.name === configName);
        if (isDefinedNotNull(config)) {
          const configData = [];
          const storageConfig = storageConfigTypes[config.name];
          if (!isDefinedNotNull(storageConfig)) {
            return;
          }
          const fieldAttrs = storageConfigTypes[config.name]['fields'];
          for (const configItem in config.data) {
            const configAttr = fieldAttrs.find((item) => item.id === configItem);
            if (isDefinedNotNull(configAttr)) {
              configData.push(
                {name: configAttr.label, data: config.data[configItem]}
              );
            }
          }

          const configFields = [];
          const configTemplate = storageConfigTypes[configName];
          configTemplate.fields.forEach((field)=> {
            const value = config.data[field.id];
            configFields.push(
              <Row className="config-provider-row" key={configName + field.id}>
                <Col lg={2}>
                  <div className="form-item-custom-label">{field.label}</div>
                </Col>
                <Col lg={10}>
                  <Field name={field.id} placeHolder={field.placeHolder}
                         input={{value: value, disabled: isDefinedNotNull(value)}}
                         component={YBTextInput} className={"data-cell-input"}/>
                </Col>
              </Row>
            );
          });

          const configControls = (<div>
            <YBButton btnText={"Delete Configuration"} disabled={ submitting || loading || isEmptyObject(config) }
                      btnClass={"btn btn-default"}
                      onClick={isDefinedNotNull(config) ? this.showDeleteConfirmModal.bind(this, config.name) : () => {}}/>
            {isDefinedNotNull(config) && <YBConfirmModal name="delete-storage-config" title={"Confirm Delete"}
                            onConfirm={handleSubmit(this.deleteStorageConfig.bind(this, config.configUUID))}
                            currentModal={"delete" + config.name + "StorageConfig"}
                            visibleModal={this.props.visibleModal}
                            hideConfirmModal={this.props.hideDeleteStorageConfig}>
              Are you sure you want to delete {config.name} Storage Configuration?
            </YBConfirmModal>}
          </div>);


          configs.push(
            this.wrapFields(configFields, configName, configControls)
          );

        } else {

          const configFields = [];
          const config = storageConfigTypes[configName];
          config.fields.forEach((field)=> {
            configFields.push(
              <Row className="config-provider-row" key={configName + field.id}>
                <Col lg={2}>
                  <div className="form-item-custom-label">{field.label}</div>
                </Col>
                <Col lg={10}>
                  <Field name={field.id} placeHolder={field.placeHolder}
                        component={YBTextInput} className={"data-cell-input"}/>
                </Col>
              </Row>
            );
          });

          configs.push(
            this.wrapFields(configFields, configName)
          );
        }
      });

      const activeTab = this.props.activeTab || Object.keys(storageConfigTypes)[0].toLowerCase();
      const config = this.getConfigByType(activeTab, customerConfigs);

      return (
        <div className="provider-config-container">
          <form name="storageConfigForm" onSubmit={handleSubmit(this.addStorageConfig)}>
            { error && <Alert bsStyle="danger">{error}</Alert> }
            <YBTabsPanel
                defaultTab={Object.keys(storageConfigTypes)[0].toLowerCase()}
                activeTab={activeTab} id="storage-config-tab-panel"
                className="config-tabs" routePrefix="/config/backup/">
              { configs }
            </YBTabsPanel>

            <div className="form-action-button-container">
              <YBButton btnText={"Save"} btnClass={"btn btn-orange"}
                        disabled={ submitting || loading || isNonEmptyObject(config) } btnType="submit"/>
            </div>
          </form>
        </div>
      );
    }
    return <YBLoading />;
  }
}

export default withRouter(StorageConfiguration);
