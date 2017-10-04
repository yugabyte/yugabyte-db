// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { YBConfirmModal } from '../../../modals';
import {TaskProgressContainer} from '../../../tasks';
import { reduxForm } from 'redux-form';

class ProviderBootstrapView extends Component {
  constructor(props) {
    super(props);
    this.deleteProviderConfig = this.deleteProviderConfig.bind(this);
    this.showDeleteProviderModal = this.showDeleteProviderModal.bind(this);
  }

  deleteProviderConfig(provider) {
    this.props.deleteProviderConfig(provider.uuid);
  }

  showDeleteProviderModal() {
    const {providerType, showDeleteProviderModal} = this.props;
    if (providerType === "aws") {
      showDeleteProviderModal("deleteAWSProvider");
    } else {
      showDeleteProviderModal("deleteGCPProvider");
    }
  }

  render() {
    const {currentProvider, handleSubmit, providerType, currentModal, visibleModal} = this.props;
    // Delete Button is never disabled in Bootstrap view for now, in future add disable if task is pending
    const deleteButtonDisabled = false;
    const deleteButtonClassName = "btn btn-default manage-provider-btn delete-btn";
    return (
      <div>
        <Row className="config-section-header">
          <Col md={12}>
            <span className="create-provider-label-text">Creating Provider </span>
            <span className="pull-right" title={`Delete ${providerType.toUpperCase()} Config`}>
              <YBButton btnText="Delete Configuration" disabled={deleteButtonDisabled}
                        btnClass={deleteButtonClassName} onClick={this.showDeleteProviderModal}/>
              <YBConfirmModal name="deleteProvider" title={"Confirm Delete"}
                              onConfirm={handleSubmit(this.deleteProviderConfig.bind(this, currentProvider))}
                              currentModal={currentModal} visibleModal={visibleModal}
                              hideConfirmModal={this.props.hideDeleteProviderModal}>
                Are you sure you want to delete this {providerType.toUpperCase()} configuration?
              </YBConfirmModal>
            </span>
          </Col>
        </Row>
        <Row>
          <Col md={12}>
            <div className="provider-task-progress-container">
              <TaskProgressContainer type={"StepBar"} taskUUIDs={this.props.taskUUIDs} onTaskSuccess={this.props.reloadCloudMetadata}/>
            </div>
          </Col>
        </Row>
      </div>
    );
  }
}

export default reduxForm({
  form: 'deleteProvider'
})(ProviderBootstrapView);
