// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { reduxForm } from 'redux-form';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { DescriptionList } from '../../../common/descriptors';
import { RegionMap, YBMapLegend } from '../../../maps';
import { YBConfirmModal } from '../../../modals';
import EditProviderFormContainer from './EditProvider/EditProviderFormContainer';
import { PROVIDER_TYPES } from '../../../../config';
import { CloudType } from '../../../../redesign/helpers/dtos';
import { ChangeOrAddProvider } from './ChangeOrAddProvider/ChangeOrAddProvider';

const cloudProviders = new Set([CloudType.aws, CloudType.gcp, CloudType.azu]);

class ProviderResultView extends Component {
  constructor(props) {
    super(props);
    this.state = { refreshing: false, currentView: 'success' };
  }

  refreshPricingData = (provider) => {
    this.props.initializeMetadata(provider.uuid);
    this.setState({ refreshing: true });
  };

  editProviderView = (provider) => {
    this.setState({ currentView: 'edit' });
  };

  showDeleteProviderModal = () => {
    const { providerType, showDeleteProviderModal } = this.props;
    switch (providerType) {
      case 'aws':
        showDeleteProviderModal('deleteAWSProvider');
        break;
      case 'gcp':
        showDeleteProviderModal('deleteGCPProvider');
        break;
      case 'azu':
        showDeleteProviderModal('deleteAzureProvider');
        break;
      default:
        break;
    }
  };

  componentDidUpdate(prevProps) {
    if (this.props.refreshSucceeded === true && prevProps.refreshSucceeded === false) {
      this.setState({ refreshing: false });
    }
  }

  getCurrentProviderPayload = () => {
    const { currentProvider, providerInfo } = this.props;
    const payload = {};
    payload.uuid = currentProvider.uuid;
    payload.code = currentProvider.code;
    payload.hostedZoneId = currentProvider.config?.HOSTED_ZONE_ID || '';
    payload.accountName = providerInfo.find((a) => a.name === 'Name').data;
    payload.sshKey = providerInfo.find((a) => a.name === 'SSH Key').data;
    return payload;
  };

  switchToResultView = () => {
    this.setState({ currentView: 'success' });
  };

  render() {
    const {
      regions,
      deleteButtonTitle,
      currentProvider,
      handleSubmit,
      providerInfo,
      buttonBaseClassName,
      currentModal,
      providerType,
      deleteButtonDisabled,
      configuredProviders,
      selectProvider,
      setCurrentViewCreateConfig,
      featureFlags,
      handleDeleteProviderConfig
    } = this.props;
    const providerMeta = PROVIDER_TYPES.find((item) => item.code === providerType);
    const { refreshing } = this.state;
    const deleteButtonClassName = 'btn btn-default manage-provider-btn';
    const deleteDisabled = deleteButtonDisabled || this.state.refreshing;
    let refreshPricingLabel = 'Refresh Pricing Data';
    if (refreshing) {
      refreshPricingLabel = 'Refreshing...';
    }
    const editProviderPayload = this.getCurrentProviderPayload();
    if (this.state.currentView === 'edit') {
      return (
        <EditProviderFormContainer
          {...editProviderPayload}
          switchToResultView={this.switchToResultView}
        />
      );
    }
    return (
      <div className="provider-config-container">
        {(featureFlags.test.addListMultiProvider || featureFlags.released.addListMultiProvider) && (
          <ChangeOrAddProvider
            configuredProviders={configuredProviders}
            cloudProviders={cloudProviders}
            selectProvider={selectProvider}
            providerType={providerType}
            setCurrentViewCreateConfig={setCurrentViewCreateConfig}
          />
        )}
        <Row className="config-section-header">
          <Col md={12}>
            <span className="pull-right buttons" title={deleteButtonTitle}>
              <YBButton
                btnText="Delete Configuration"
                disabled={deleteDisabled}
                btnClass={deleteButtonClassName}
                onClick={this.showDeleteProviderModal}
              />
              <YBButton
                btnText={refreshPricingLabel}
                btnClass={buttonBaseClassName}
                disabled={refreshing}
                onClick={this.refreshPricingData.bind(this, currentProvider)}
              />
              <YBButton btnText="Edit Configuration" onClick={this.editProviderView} />
              <YBConfirmModal
                name="deleteProvider"
                title={'Confirm Delete'}
                onConfirm={handleSubmit(() => {
                  handleDeleteProviderConfig(currentProvider.uuid);
                })}
                currentModal={currentModal}
                visibleModal={this.props.visibleModal}
                hideConfirmModal={this.props.hideDeleteProviderModal}
              >
                Are you sure you want to delete this {providerMeta.label} configuration?
              </YBConfirmModal>
            </span>
            <DescriptionList listItems={providerInfo} />
          </Col>
        </Row>
        <Row>
          <Col lg={12} className="provider-map-container">
            <RegionMap
              title="All Supported Regions"
              regions={regions}
              type="Region"
              showLabels={true}
              showRegionLabels={true}
            />
            <YBMapLegend title="Region Map" />
          </Col>
        </Row>
      </div>
    );
  }
}

export default reduxForm({
  form: 'deleteProvider'
})(ProviderResultView);
