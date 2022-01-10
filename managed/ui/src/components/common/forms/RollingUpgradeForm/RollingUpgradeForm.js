// Copyright (c) YugaByte, Inc.

import _ from 'lodash';
import React, { Component } from 'react';
import { Field, FieldArray } from 'redux-form';
import { Col, Alert } from 'react-bootstrap';
import { YBModal, YBInputField, YBSelectWithLabel, YBToggle, YBCheckBox } from '../fields';
import { isNonEmptyArray, get } from '../../../../utils/ObjectUtils';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import { getPrimaryCluster } from '../../../../utils/UniverseUtils';
import { isDefinedNotNull, isNonEmptyObject } from '../../../../utils/ObjectUtils';
import './RollingUpgradeForm.scss';
import { EncryptionInTransit } from './EncryptionInTransit';
import GFlagComponent from '../../../universes/UniverseForm/GFlagComponent';
import { FlexShrink, FlexContainer } from '../../flexbox/YBFlexBox';

export default class RollingUpgradeForm extends Component {
  constructor(props) {
    super(props);
    this.state = { formConfirmed: false };
  }

  toggleConfirmValidation = () => {
    this.setState({ formConfirmed: !this.state.formConfirmed });
  };

  componentWillUnmount() {
    this.resetAndClose();
  }

  getCurrentVersion = () => {
    const { universe } = this.props;
    let currentVersion = null;
    if (
      isDefinedNotNull(universe.currentUniverse.data) &&
      isNonEmptyObject(universe.currentUniverse.data)
    ) {
      const primaryCluster = getPrimaryCluster(
        universe.currentUniverse.data.universeDetails.clusters
      );
      currentVersion = primaryCluster?.userIntent?.ybSoftwareVersion;
    }
    return currentVersion;
  };

  setRollingUpgradeProperties = (values) => {
    const {
      modal: { visibleModal },
      universe: {
        currentUniverse: {
          data: {
            universeDetails: { clusters, nodePrefix },
            universeUUID
          }
        }
      }
    } = this.props;

    const payload = {};
    switch (visibleModal) {
      case 'softwareUpgradesModal': {
        payload.taskType = 'Software';
        payload.upgradeOption = values.rollingUpgrade ? 'Rolling' : 'Non-Rolling';
        break;
      }
      case 'systemdUpgrade': {
        payload.taskType = 'Systemd';
        payload.upgradeOption = 'Rolling';
        var systemdBoolean = true;
        break;
      }
      case 'gFlagsModal': {
        payload.taskType = 'GFlags';
        payload.upgradeOption = values.upgradeOption;
        break;
      }
      case 'tlsConfigurationModal': {
        payload.taskType = 'Certs';
        payload.upgradeOption = values.rollingUpgrade ? 'Rolling' : 'Non-Rolling';
        payload.certUUID = values.tlsCertificate;
        break;
      }
      case 'rollingRestart': {
        payload.taskType = 'Restart';
        payload.upgradeOption = 'Rolling';
        break;
      }
      default:
        return;
    }

    const primaryCluster = _.cloneDeep(getPrimaryCluster(clusters));
    if (!isDefinedNotNull(primaryCluster)) {
      return;
    }
    payload.ybSoftwareVersion = values.ybSoftwareVersion;
    payload.universeUUID = universeUUID;
    payload.nodePrefix = nodePrefix;
    const masterGFlagList = [];
    const tserverGFlagList = [];
    if (isNonEmptyArray(values?.gFlags)) {
      values.gFlags.forEach((flag) => {
        if (get(flag, 'MASTER', null))
          masterGFlagList.push({ name: flag?.Name, value: flag['MASTER'] });
        if (get(flag, 'TSERVER', null))
          tserverGFlagList.push({ name: flag?.Name, value: flag['TSERVER'] });
      });
    }
    primaryCluster.userIntent.ybSoftwareVersion = values.ybSoftwareVersion;
    primaryCluster.userIntent.masterGFlags = masterGFlagList;
    primaryCluster.userIntent.tserverGFlags = tserverGFlagList;
    primaryCluster.userIntent.useSystemd = systemdBoolean;
    payload.clusters = [primaryCluster];
    payload.sleepAfterMasterRestartMillis = values.timeDelay * 1000;
    payload.sleepAfterTServerRestartMillis = values.timeDelay * 1000;

    this.props.submitRollingUpgradeForm(payload, universeUUID).then((response) => {
      if (response.payload.status === 200) {
        this.props.fetchCurrentUniverse(universeUUID);
        this.props.fetchUniverseMetadata();
        this.props.fetchCustomerTasks();
        this.props.fetchUniverseTasks(universeUUID);
        this.resetAndClose();
      }
    });
  };

  resetAndClose = () => {
    this.props.onHide();
    this.props.reset();
    this.props.resetRollingUpgrade();
    this.setState({ formConfirmed: false });
  };

  render() {
    const {
      modalVisible,
      handleSubmit,
      universe,
      modal: { visibleModal },
      universe: { error },
      softwareVersions,
      formValues,
      certificates
    } = this.props;

    const currentVersion = this.getCurrentVersion();
    const submitAction = handleSubmit(this.setRollingUpgradeProperties);

    const softwareVersionOptions = softwareVersions.map((item, idx) => (
      <option key={idx} disabled={item === currentVersion} value={item}>
        {item}
      </option>
    ));

    const tlsCertificateOptions = certificates.map((item) => (
      <option
        key={item.uuid}
        disabled={item.uuid === universe.currentUniverse?.data?.universeDetails?.rootCA}
        value={item.uuid}
      >
        {item.label}
      </option>
    ));

    const errorAlert = getPromiseState(universe.rollingUpgrade).isError() && (
      <Alert bsStyle="danger" variant="danger">
        {universe.rollingUpgrade.error
          ? JSON.stringify(universe.rollingUpgrade.error)
          : 'Something went wrong'}
      </Alert>
    );

    switch (visibleModal) {
      case 'softwareUpgradesModal': {
        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            submitLabel="Upgrade"
            showCancelButton
            title="Upgrade Software"
            onFormSubmit={submitAction}
            error={error}
            footerAccessory={
              formValues.ybSoftwareVersion !== currentVersion ? (
                <YBCheckBox
                  label="Confirm software upgrade"
                  input={{
                    checked: this.state.formConfirmed,
                    onChange: this.toggleConfirmValidation
                  }}
                />
              ) : (
                <span>Latest software is installed</span>
              )
            }
            asyncValidating={!this.state.formConfirmed}
          >
            <Col lg={12} className="form-section-title">
              Software Package Version
            </Col>
            <div className="form-right-aligned-labels rolling-upgrade-form">
              <Field name="rollingUpgrade" component={YBToggle} label="Rolling Upgrade" />
              <Field
                name="timeDelay"
                type="number"
                component={YBInputField}
                label="Upgrade Delay Between Servers (secs)"
              />
              <Field
                name="ybSoftwareVersion"
                type="select"
                component={YBSelectWithLabel}
                options={softwareVersionOptions}
                label="Server Version"
              />
            </div>
            {errorAlert}
          </YBModal>
        );
      }
      case 'gFlagsModal': {
        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            title="Flags"
            size="large"
            onFormSubmit={submitAction}
            error={error}
          >
            <FieldArray name="gFlags" component={GFlagComponent} dbVersion={currentVersion} />
            <FlexContainer className="gflag-upgrade-container">
              <FlexShrink>
                <span className="gflag-upgrade--label">G-Flag Upgrade Options</span>
              </FlexShrink>
              <div className="gflag-upgrade-options">
                {['Rolling', 'Non-Rolling', 'Non-Restart'].map((target) => (
                  <span className="btn-group btn-group-radio upgrade-option" key={target}>
                    <Field
                      name={'upgradeOption'}
                      type="radio"
                      component="input"
                      value={`${target}`}
                    />{' '}
                    {`${target}`}{' '}
                  </span>
                ))}
              </div>
            </FlexContainer>
            {errorAlert}
          </YBModal>
        );
      }
      case 'tlsConfigurationModal': {
        if (this.props.enableNewEncryptionInTransitModal) {
          return (
            <EncryptionInTransit
              visible={modalVisible}
              onHide={this.resetAndClose}
              currentUniverse={universe.currentUniverse}
              fetchCurrentUniverse={this.props.fetchCurrentUniverse}
            />
          );
        }
        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            submitLabel="Upgrade"
            showCancelButton
            title="Edit TLS Configuration"
            onFormSubmit={submitAction}
            error={error}
            footerAccessory={
              formValues.tlsCertificate !==
              universe.currentUniverse?.data?.universeDetails?.rootCA ? (
                <YBCheckBox
                  label="Confirm TLS Changes"
                  input={{
                    checked: this.state.formConfirmed,
                    onChange: this.toggleConfirmValidation
                  }}
                />
              ) : (
                <span>Select new CA signed cert from the list</span>
              )
            }
            asyncValidating={
              !this.state.formConfirmed ||
              formValues.tlsCertificate === universe.currentUniverse?.data?.universeDetails?.rootCA
            }
          >
            <div className="form-right-aligned-labels rolling-upgrade-form">
              <Field
                name="tlsCertificate"
                type="select"
                component={YBSelectWithLabel}
                options={tlsCertificateOptions}
                label="New TLS Certificate"
              />
              <Field
                name="timeDelay"
                type="number"
                required
                component={YBInputField}
                label="Upgrade Delay Between Servers (secs)"
              />
              <Field name="rollingUpgrade" component={YBToggle} label="Rolling Upgrade" />
            </div>
            {errorAlert}
          </YBModal>
        );
      }
      case 'rollingRestart': {
        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            submitLabel="Restart"
            showCancelButton
            title="Initiate Rolling Restart"
            onFormSubmit={submitAction}
            error={error}
            footerAccessory={
              <YBCheckBox
                label="Confirm rolling restart"
                input={{
                  checked: this.state.formConfirmed,
                  onChange: this.toggleConfirmValidation
                }}
              />
            }
            asyncValidating={!this.state.formConfirmed}
          >
            <div className="form-right-aligned-labels rolling-upgrade-form">
              <Field
                name="timeDelay"
                type="number"
                component={YBInputField}
                label="Rolling Restart Delay Between Servers (secs)"
              />
            </div>
            {errorAlert}
          </YBModal>
        );
      }
      case 'systemdUpgrade': {
        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            submitLabel="Upgrade"
            showCancelButton
            title="Upgrade from Cron to Systemd"
            onFormSubmit={submitAction}
            error={error}
            footerAccessory={
              formValues.systemdValue !== true ? (
                <YBCheckBox
                  label="Confirm Systemd upgrade"
                  input={{
                    checked: this.state.formConfirmed,
                    onChange: this.toggleConfirmValidation
                  }}
                />
              ) : (
                <span>Already upgraded to Systemd</span>
              )
            }
            asyncValidating={!this.state.formConfirmed}
          >
            <div className="form-right-aligned-labels rolling-upgrade-form">
              <Field
                name="timeDelay"
                type="number"
                component={YBInputField}
                label="Rolling Restart Delay Between Servers (secs)"
              />
            </div>
            {errorAlert}
          </YBModal>
        );
      }
      default: {
        return null;
      }
    }
  }
}
