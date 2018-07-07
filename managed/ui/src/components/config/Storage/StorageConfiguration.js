// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBTextInput, YBButton } from '../../common/forms/fields';
import {withRouter} from 'react-router';
import { Field } from 'redux-form';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import { DescriptionList } from '../../common/descriptors';
import { YBConfirmModal } from '../../modals';
import { isDefinedNotNull } from "utils/ObjectUtils";

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
  addStorageConfig = values => {
    // TODO: need to split config into AWS vs NFS type in UI.
    this.props.addCustomerConfig({
      "type": "STORAGE",
      "name": "AWS_ACCESS_KEY_ID" in values ? "S3" : "NFS",
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
    if (getPromiseState(customerConfigs).isSuccess() && customerConfigs.data.length > 0) {
      const configs = [];
      const configData = [];

      customerConfigs.data.forEach((config) => {
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

        configs.push(
          <Col md={12} key={config.name}>
            <span className="pull-right" title={"Delete this configuration."}>
              <YBButton btnText="Delete Configuration"
                        btnClass={"btn btn-default delete-btn"}
                        onClick={this.showDeleteConfirmModal.bind(this, config.name)}/>
              <YBConfirmModal name="delete-storage-config" title={"Confirm Delete"}
                              onConfirm={handleSubmit(this.deleteStorageConfig.bind(this, config.configUUID))}
                              currentModal={"delete" + config.name + "StorageConfig"}
                              visibleModal={this.props.visibleModal}
                              hideConfirmModal={this.props.hideDeleteStorageConfig}>
                Are you sure you want to delete {config.name} Storage Configuration?
              </YBConfirmModal>
            </span>
            <DescriptionList listItems={configData}/>
          </Col>
        );
      });

      return (
        <div className="provider-config-container">
          <Row className="config-section-header">
            {configs}
          </Row>
        </div>
      );
    }

    const configs = [];
    Object.keys(storageConfigTypes).forEach((configName) => {
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
        <Row className="config-section-header" key={configName}>
          <Col lg={8}>
            {configFields}
          </Col>
        </Row>
      );
    });

    return (
      <div className="provider-config-container">
        <form name="storageConfigForm" onSubmit={handleSubmit(this.addStorageConfig)}>
          { error && <Alert bsStyle="danger">{error}</Alert> }
          { configs }
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}
                      disabled={submitting || loading } btnType="submit"/>
          </div>
        </form>
      </div>
    );
  }
}

export default withRouter(StorageConfiguration);
