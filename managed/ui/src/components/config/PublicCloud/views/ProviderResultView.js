// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { DescriptionList } from '../../../common/descriptors';
import { RegionMap, YBMapLegend } from '../../../maps';
import { YBConfirmModal } from '../../../modals';
import {reduxForm} from 'redux-form';
import EditProviderFormContainer from './EditProvider/EditProviderFormContainer';

class ProviderResultView extends Component {
  constructor(props) {
    super(props);
    this.state = {refreshing: false, currentView: 'success'};
  }

  deleteProviderConfig(provider) {
    this.props.deleteProviderConfig(provider.uuid);
  }

  refreshPricingData = provider => {
    this.props.initializeMetadata(provider.uuid);
    this.setState({refreshing: true});
  };

  editProviderView = (provider) => {
    this.setState({currentView: 'edit'});
  }

  showDeleteProviderModal = () => {
    const {providerType, showDeleteProviderModal} = this.props;
    if (providerType === "aws") {
      showDeleteProviderModal("deleteAWSProvider");
    } else {
      showDeleteProviderModal("deleteGCPProvider");
    }
  };

  componentDidUpdate(prevProps) {
    if (this.props.refreshSucceeded === true && prevProps.refreshSucceeded === false) {
      this.setState({refreshing: false});
    }
  };

  getCurrentProviderPayload = () => {
    const {currentProvider, providerInfo} = this.props;
    const payload = {};
    payload.uuid = currentProvider.uuid;
    payload.code = currentProvider.code;
    payload.hostedZoneId = currentProvider.code === "aws" ? currentProvider.config.AWS_HOSTED_ZONE_ID : "";
    payload.accountName = providerInfo.find((a)=>(a.name === "Name")).data;
    payload.sshKey = providerInfo.find((a)=>(a.name === "SSH Key")).data;
    return payload;
  };

  switchToResultView = () => {
    this.setState({currentView: 'success'});
  };

  render() {
    const { regions, deleteButtonTitle, currentProvider, handleSubmit,
            providerInfo, buttonBaseClassName, currentModal,
            providerType, deleteButtonDisabled} = this.props;
    const {refreshing} = this.state;
    const deleteButtonClassName = "btn btn-default manage-provider-btn";
    const deleteDisabled = deleteButtonDisabled || this.state.refreshing;
    let refreshPricingLabel = "Refresh Pricing Data";
    if (refreshing) {
      refreshPricingLabel = "Refreshing...";
    }
    const editProviderPayload = this.getCurrentProviderPayload();
    if (this.state.currentView === "edit") {
      return <EditProviderFormContainer {...editProviderPayload} switchToResultView={this.switchToResultView}/>;
    }
    return (
      <div className="provider-config-container">
        <Row className="config-section-header">
          <Col md={12}>
            <span className="pull-right buttons" title={deleteButtonTitle}>
              <YBButton btnText="Delete Configuration" disabled={deleteDisabled}
                        btnClass={deleteButtonClassName} onClick={this.showDeleteProviderModal}/>
              <YBButton btnText={refreshPricingLabel} btnClass={buttonBaseClassName} disabled={refreshing}
                      onClick={this.refreshPricingData.bind(this, currentProvider)}/>
              <YBButton btnText="Edit Configuration"
                        onClick={this.editProviderView.bind(this, currentProvider)}/>
              <YBConfirmModal name="deleteProvider" title={"Confirm Delete"}
                              onConfirm={handleSubmit(this.deleteProviderConfig.bind(this, currentProvider))}
                              currentModal={currentModal} visibleModal={this.props.visibleModal}
                              hideConfirmModal={this.props.hideDeleteProviderModal}>
                Are you sure you want to delete this {providerType.toUpperCase()} configuration?
              </YBConfirmModal>
            </span>
            <DescriptionList listItems={providerInfo}/>
          </Col>
        </Row>
        <Row>
          <Col lg={12} className="provider-map-container">
            <RegionMap title="All Supported Regions" regions={regions} type="Region" showLabels={true} showRegionLabels={true} />
            <YBMapLegend title="Region Map"/>
          </Col>
        </Row>
      </div>
    );
  }
}

export default reduxForm({
  form: 'deleteProvider'
})(ProviderResultView);
