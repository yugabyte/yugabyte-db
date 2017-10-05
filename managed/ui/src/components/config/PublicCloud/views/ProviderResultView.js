// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { DescriptionList } from '../../../common/descriptors';
import { RegionMap, YBMapLegend } from '../../../maps';
import { YBConfirmModal } from '../../../modals';
import {reduxForm} from 'redux-form';

class ProviderResultView extends Component {
  constructor(props) {
    super(props);
    this.refreshPricingData = this.refreshPricingData.bind(this);
    this.showDeleteProviderModal = this.showDeleteProviderModal.bind(this);
    this.state = {refreshing: false};
  }

  deleteProviderConfig(provider) {
    this.props.deleteProviderConfig(provider.uuid);
  }

  refreshPricingData(provider) {
    this.props.initializeMetadata(provider.uuid);
    this.setState({refreshing: true});
  }

  showDeleteProviderModal() {
    const {providerType, showDeleteProviderModal} = this.props;
    if (providerType === "aws") {
      showDeleteProviderModal("deleteAWSProvider");
    } else {
      showDeleteProviderModal("deleteGCPProvider");
    }
  }

  componentWillReceiveProps(nextProps) {
    if (nextProps.refreshSucceeded === true && this.props.refreshSucceeded == false) {
      this.setState({refreshing: false});
    }
  }

  render() {
    const { regions, deleteButtonTitle, currentProvider, handleSubmit,
            providerInfo, buttonBaseClassName, currentModal,
            providerType, deleteButtonDisabled} = this.props;
    const {refreshing} = this.state;
    const deleteButtonClassName = "btn btn-default manage-provider-btn delete-btn";
    const deleteDisabled = deleteButtonDisabled || this.state.refreshing;
    let refreshPricingLabel = "Refresh Pricing Data";
    if (refreshing) {
      refreshPricingLabel = "Refreshing...";
    }
    return (
      <div className="provider-config-container">
        <Row className="config-section-header">
          <Col md={12}>
            <span className="pull-right" title={deleteButtonTitle}>
              <YBButton btnText="Delete Configuration" disabled={deleteDisabled}
                        btnClass={deleteButtonClassName} onClick={this.showDeleteProviderModal}/>
              <YBButton btnText={refreshPricingLabel} btnClass={buttonBaseClassName} disabled={refreshing}
                        onClick={this.refreshPricingData.bind(this, currentProvider)}/>
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
