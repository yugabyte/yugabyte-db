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

import awss3Logo from './images/aws-s3.png';
import { isNonEmptyObject, isEmptyObject } from '../../../utils/ObjectUtils';

const storageConfigTypes = {
  S3: {
    title: "S3 Storage",
    fields: [{
      id: "AWS_ACCESS_KEY_ID",
      label: "Access Key",
      placeHolder: "AWS Access Key"
    }, {
      id: "AWS_SECRET_ACCESS_KEY",
      label: "Access Secret",
      placeHolder: "AWS Access Secret"
    }, {
      id: "S3_BUCKET",
      label: "S3 Bucket",
      placeHolder: "S3 Bucket"
    }]
  },
  NFS: {
    title: "NFS Storage",
    fields: [{
      id: "NFS_PATH",
      label: "NFS Storage Path",
      placeHolder: "NFS Storage Path"
    }]
  }
};

class StorageConfiguration extends Component {

  getTabTitle = (configName) => {
    switch (configName) {
      case "S3":
        return <img src={awss3Logo} alt="AWS S3" className="aws-logo" />;
      default:
        return <h3><i className="fa fa-database"></i>NFS</h3>;
    }
  }

  getConfigByType = (name, customerConfigs) => {
    return customerConfigs.data.find(config => config.name.toLowerCase() === name);
  }

  wrapFields = (configFields, configName, configControls) => {
    const configNameFormatted = configName.toLowerCase();
    return (
      <Tab eventKey={configNameFormatted} title={ this.getTabTitle(configName) } key={configNameFormatted+"-tab"} unmountOnExit={true}>
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
    const type = props.activeTab.toUpperCase() || Object.keys(storageConfigTypes)[0];
    this.props.addCustomerConfig({
      "type": "STORAGE",
      "name": type,
      "data": values
    });
  }

  deleteStorageConfig = configUUID => {
    this.props.deleteCustomerConfig(configUUID);
  }

  showDeleteConfirmModal = configName => {
    this.props.showDeleteStorageConfig(configName);
  }

  componentWillMount() {
    this.props.fetchCustomerConfigs();
  }

  componentWillReceiveProps(nextProps) {
    const { addConfig, deleteConfig } = nextProps;
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
      const configs = [];
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
                  <Field name={field.id} placeHolder={field.placeHolder} input={{value: value, disabled: isDefinedNotNull(value)}} 
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
