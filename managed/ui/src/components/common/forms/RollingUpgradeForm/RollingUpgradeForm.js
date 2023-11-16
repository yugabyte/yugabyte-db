// Copyright (c) YugaByte, Inc.

import _ from 'lodash';
import { Component } from 'react';
import { toast } from 'react-toastify';
import { Field, FieldArray } from 'redux-form';
import { Col, Alert } from 'react-bootstrap';
import clsx from 'clsx';

import { YBModal, YBInputField, YBSelectWithLabel, YBToggle, YBCheckBox } from '../fields';
import {
  createErrorMessage,
  isNonEmptyArray,
  isDefinedNotNull,
  isNonEmptyObject
} from '../../../../utils/ObjectUtils';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import {
  isKubernetesUniverse,
  getPrimaryCluster,
  getReadOnlyCluster,
  getUniverseRegions
} from '../../../../utils/UniverseUtils';
import { EncryptionInTransit } from './EncryptionInTransit';
import GFlagComponent from '../../../universes/UniverseForm/GFlagComponent';
import { FlexShrink, FlexContainer } from '../../flexbox/YBFlexBox';
import { TASK_LONG_TIMEOUT } from '../../../tasks/constants';
import { sortVersion } from '../../../releases';
import { HelmOverridesModal } from '../../../universes/UniverseForm/HelmOverrides';
import { YBBanner, YBBannerVariant } from '../../descriptors';
import { hasLinkedXClusterConfig } from '../../../xcluster/ReplicationUtils';

import { hasNecessaryPerm } from '../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../redesign/features/rbac/ApiAndUserPermMapping';

import './RollingUpgradeForm.scss';

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
            universeDetails: { currentClusterType, clusters, nodePrefix, rootAndClientRootCASame },
            universeUUID
          }
        }
      },
      overrideIntentParams,
      resetLocation,
      featureFlags
    } = this.props;
    let systemdBoolean = false;

    const payload = {};
    payload.clusters = [];
    const asyncCluster = _.cloneDeep(getReadOnlyCluster(clusters));
    switch (visibleModal) {
      case 'softwareUpgradesModal': {
        payload.taskType = 'Software';
        payload.upgradeOption = values.rollingUpgrade ? 'Rolling' : 'Non-Rolling';
        break;
      }
      case 'vmImageUpgradeModal': {
        const regionList = getUniverseRegions(clusters);
        const regionAMIs = regionList.reduce(
          (prev, curRegion) => ({
            ...prev,
            [curRegion.uuid]: values[curRegion.uuid]
          }),
          {}
        );
        payload.taskType = 'VMImage';
        payload.upgradeOption = 'Rolling';
        payload.machineImages = regionAMIs;
        if (isNonEmptyObject(asyncCluster)) {
          payload.clusters.push(asyncCluster);
        }
        break;
      }
      case 'systemdUpgrade': {
        payload.taskType = 'Systemd';
        payload.upgradeOption = 'Rolling';
        systemdBoolean = true;
        break;
      }
      case 'gFlagsModal': {
        payload.taskType = 'GFlags';
        payload.upgradeOption = values.upgradeOption;
        break;
      }
      case 'tlsConfigurationModal': {
        const cluster =
          currentClusterType === 'PRIMARY'
            ? getPrimaryCluster(clusters)
            : getReadOnlyCluster(clusters);
        payload.taskType = 'Certs';
        payload.upgradeOption = 'Rolling';
        payload.enableNodeToNodeEncrypt = cluster.userIntent.enableNodeToNodeEncrypt;
        payload.enableClientToNodeEncrypt = cluster.userIntent.enableClientToNodeEncrypt;
        payload.rootAndClientRootCASame = rootAndClientRootCASame;
        payload.rootCA =
          values.tlsCertificate === 'Create New Certificate' ? null : values.tlsCertificate;
        payload.createNewRootCA = values.tlsCertificate === 'Create New Certificate';
        break;
      }
      case 'rollingRestart': {
        payload.taskType = 'Restart';
        payload.upgradeOption = 'Rolling';
        //send read replica clsuter details in payload only for k8s universe
        if (
          isKubernetesUniverse(this.props.universe.currentUniverse.data) &&
          isNonEmptyObject(asyncCluster)
        )
          payload.clusters.push(asyncCluster);
        break;
      }
      case 'resizeNodesModal': {
        payload.taskType = 'Resize_Node';
        payload.upgradeOption = 'Rolling';
        break;
      }
      case 'thirdpartyUpgradeModal': {
        payload.taskType = 'Thirdparty_Software';
        payload.upgradeOption = 'Rolling';
        break;
      }
      case 'helmOverridesModal':
        payload.taskType = 'kubernetes_overrides';
        payload.universeOverrides = values.universeOverrides;
        payload.azOverrides = values.azOverrides;
        break;
      default:
        return;
    }

    const primaryCluster = _.cloneDeep(getPrimaryCluster(clusters));
    if (!isDefinedNotNull(primaryCluster)) {
      return;
    }
    if (payload.taskType !== 'VMImage') {
      payload.ybSoftwareVersion = values.ybSoftwareVersion;
    }
    payload.universeUUID = universeUUID;
    payload.nodePrefix = nodePrefix;
    const masterGFlagList = [];
    const tserverGFlagList = [];
    if (isNonEmptyArray(values?.gFlags)) {
      values.gFlags.forEach((flag) => {
        if (Object.prototype.hasOwnProperty.call(flag, 'MASTER')) {
          masterGFlagList.push({ name: flag?.Name, value: flag['MASTER'] });
        }
        if (Object.prototype.hasOwnProperty.call(flag, 'TSERVER')) {
          tserverGFlagList.push({ name: flag?.Name, value: flag['TSERVER'] });
        }
      });
    }
    primaryCluster.userIntent.ybSoftwareVersion = values.ybSoftwareVersion;
    primaryCluster.userIntent.masterGFlags = masterGFlagList;
    primaryCluster.userIntent.tserverGFlags = tserverGFlagList;
    primaryCluster.userIntent.useSystemd = systemdBoolean;

    if (visibleModal === 'helmOverridesModal') {
      primaryCluster.userIntent.universeOverrides = values.universeOverrides;
      primaryCluster.userIntent.azOverrides = values.azOverrides;
    }

    payload.clusters = [primaryCluster, ...payload.clusters];
    payload.sleepAfterMasterRestartMillis = values.timeDelay * 1000;
    payload.sleepAfterTServerRestartMillis = values.timeDelay * 1000;
    if (overrideIntentParams) {
      if (overrideIntentParams.instanceType) {
        primaryCluster.userIntent.instanceType = overrideIntentParams.instanceType;
      }
      if (overrideIntentParams.volumeSize) {
        primaryCluster.userIntent.deviceInfo.volumeSize = overrideIntentParams.volumeSize;
      }
    }

    if (!isDefinedNotNull(primaryCluster.enableYbc) && payload.taskType === 'Software')
      payload.enableYbc = featureFlags.released.enableYbc || featureFlags.test.enableYbc;

    this.props.submitRollingUpgradeForm(payload, universeUUID).then((response) => {
      if (response.payload.status === 200) {
        this.props.fetchCurrentUniverse(universeUUID);
        this.props.fetchUniverseMetadata();
        this.props.fetchCustomerTasks();
        this.props.fetchUniverseTasks(universeUUID);
        if (resetLocation) {
          window.location.href = `/universes/${universeUUID}`;
        } else {
          this.resetAndClose();
        }
      } else {
        toast.error(createErrorMessage(response.payload), { autoClose: 3000 });
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
      universe: { error, supportedReleases },
      formValues,
      certificates,
      overrideIntentParams
    } = this.props;

    const currentVersion = this.getCurrentVersion();
    const submitAction = handleSubmit(this.setRollingUpgradeProperties);
    let softwareVersionOptions = [];
    if (getPromiseState(supportedReleases).isSuccess()) {
      softwareVersionOptions = (supportedReleases?.data || [])
        ?.sort(sortVersion)
        .map((item, idx) => (
          // eslint-disable-next-line react/no-array-index-key
          <option key={idx} disabled={item === currentVersion} value={item}>
            {item}
          </option>
        ));
    }

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
                <span>Selected software version already installed</span>
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
      case 'vmImageUpgradeModal': {
        const regionList = getUniverseRegions(
          universe?.currentUniverse?.data.universeDetails?.clusters
        );

        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            cancelLabel="Cancel"
            error={error}
            onFormSubmit={submitAction}
            showCancelButton={true}
            size="large"
            submitLabel="Upgrade"
            title="Upgrade VM Image"
          >
            <div className="form-right-aligned-labels rolling-upgrade-form">
              {regionList.map((region) => (
                <Field
                  key={region.uuid}
                  name={region.uuid}
                  type="text"
                  component={YBInputField}
                  label={`${region.name} AMI`}
                />
              ))}
            </div>
            {errorAlert}
          </YBModal>
        );
      }
      case 'helmOverridesModal': {
        const editValues = {};
        const { universeDetails } = this.props.universe.currentUniverse.data;
        const primaryCluster = getPrimaryCluster(universeDetails.clusters);
        const { universeOverrides, azOverrides } = primaryCluster.userIntent;
        if (universeOverrides) {
          editValues['universeOverrides'] = universeOverrides;
        }
        if (azOverrides) {
          editValues['azOverrides'] = Object.keys(azOverrides).map(
            (k) => k + `:\n${azOverrides[k]}`
          );
        }

        //no instance tags for k8s
        delete editValues.instaceTags;

        return (
          <HelmOverridesModal
            visible={true}
            onHide={this.resetAndClose}
            submitLabel="Upgrade"
            getConfiguretaskParams={() => {
              return universeDetails;
            }}
            setHelmOverridesData={(helmYaml) => {
              this.props.change('universeOverrides', helmYaml.universeOverrides);
              this.props.change('azOverrides', helmYaml.azOverrides);
              submitAction();
            }}
            editValues={editValues}
            editMode={true}
            forceUpdate={true}
          />
        );
      }
      case 'gFlagsModal': {
        //checks if tags are not runtime , runtime flags changes apply without restart
        const isNotRuntime = formValues?.gFlags.some((f) => !f?.tags?.includes('runtime'));
        const GFLAG_UPDATE_OPTIONS = [
          {
            value: 'Rolling',
            label: 'Apply all changes using a rolling restart (slower, zero downtime)'
          },
          {
            value: 'Non-Rolling',
            label:
              'Apply all changes immediately, using a concurrent restart (faster, some downtime)'
          },
          {
            value: 'Non-Restart',
            label:
              'Apply all changes which do not require a restart immediately;' +
              `${isNotRuntime
                ? 'apply remaining changes the next time the database is restarted'
                : ''
              }`
          }
        ];

        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            title="G-Flags"
            size="large"
            onFormSubmit={submitAction}
            error={error}
            dialogClassName={modalVisible ? 'gflag-modal modal-fade in' : 'modal-fade'}
            showCancelButton={true}
            submitLabel="Apply Changes"
            cancelLabel="Cancel"
          >
            <div className="gflag-modal-body">
              <FieldArray
                name="gFlags"
                component={GFlagComponent}
                dbVersion={currentVersion}
                rerenderOnEveryChange={true}
                editMode={true}
              />
              <FlexContainer className="gflag-upgrade-container">
                <FlexShrink className="gflag-upgrade--label">
                  <span>G-Flag Update Options</span>
                </FlexShrink>
                <div className="gflag-upgrade-options">
                  {GFLAG_UPDATE_OPTIONS.map((option, i) => (
                    <div key={option.value} className="row-flex">
                      <div className={clsx('upgrade-radio-option', i === 1 && 'mb-8')}>
                        <Field
                          name={'upgradeOption'}
                          type="radio"
                          component="input"
                          value={option.value}
                        />
                        <span className="upgrade-radio-label">{option.label}</span>
                      </div>
                      {i === 0 && (
                        <div className="gflag-delay">
                          <span className="vr-line">|</span>
                          Delay Between Servers :{' '}
                          <Field
                            name="timeDelay"
                            type="number"
                            component={YBInputField}
                            isReadOnly={formValues.upgradeOption !== 'Rolling'}
                          />
                          seconds
                        </div>
                      )}
                    </div>
                  ))}
                </div>
              </FlexContainer>
              <div className="gflag-err-msg">{errorAlert}</div>
            </div>
          </YBModal>
        );
      }
      case 'tlsConfigurationModal': {
        if (
          this.props.enableNewEncryptionInTransitModal &&
          !isKubernetesUniverse(universe.currentUniverse.data)
        ) {
          return (
            <EncryptionInTransit
              visible={modalVisible}
              onHide={this.resetAndClose}
              currentUniverse={universe.currentUniverse}
              fetchCurrentUniverse={this.props.fetchCurrentUniverse}
            />
          );
        }
        const universeHasXClusterConfig = hasLinkedXClusterConfig([universe.currentUniverse.data]);
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
            disableSubmit={!hasNecessaryPerm({ ...ApiPermissionMap.MODIFY_UNIVERSE_TLS, onResource: universe.currentUniverse?.data?.universeUUID })}
          >
            {universeHasXClusterConfig && isKubernetesUniverse(universe.currentUniverse.data) && (
              <YBBanner variant={YBBannerVariant.WARNING} showBannerIcon={false}>
                <b>{`Warning! `}</b>
                <p>
                  This Kubernetes universe is involved in one or more xCluster configurations.
                  Changing TLS certificates on this universe will break the xCluster replication as
                  mismatched certificates is not supported.
                </p>
                <p>
                  To enable replication again after rotating the TLS certificate on this universe,
                  you must:
                  <ol>
                    <li>
                      Configure the TLS certificates on all other participating universes to match
                      this universe.
                    </li>
                    <li>Restart all affected xCluster configurations.</li>
                  </ol>
                </p>
              </YBBanner>
            )}
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
      case 'resizeNodesModal': {
        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            showCancelButton
            onHide={this.resetAndClose}
            title="Resize Nodes"
            onFormSubmit={submitAction}
            error={error}
            footerAccessory={
              <YBCheckBox
                label="Confirm resize nodes"
                input={{
                  checked: this.state.formConfirmed,
                  onChange: this.toggleConfirmValidation
                }}
              />
            }
            asyncValidating={!this.state.formConfirmed}
          >
            <div className="form-right-aligned-labels rolling-upgrade-form top-10 time-delay-container">
              {overrideIntentParams.instanceType ? (
                <Field
                  name="timeDelay"
                  type="number"
                  component={YBInputField}
                  label="Rolling Upgrade Delay Between Servers (secs)"
                  initValue={TASK_LONG_TIMEOUT / 1000}
                />
              ) : (
                <span>This operation will be performed without restart</span>
              )}
            </div>
            {errorAlert}
          </YBModal>
        );
      }
      case 'thirdpartyUpgradeModal': {
        return (
          <YBModal
            className={getPromiseState(universe.rollingUpgrade).isError() ? 'modal-shake' : ''}
            visible={modalVisible}
            formName="RollingUpgradeForm"
            onHide={this.resetAndClose}
            submitLabel="Upgrade"
            showCancelButton
            title="Initiate Third-party Software Upgrade"
            onFormSubmit={submitAction}
            error={error}
            footerAccessory={
              <YBCheckBox
                label="Confirm third-party software upgrade"
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
                label="Rolling Upgrade Delay Between Servers (secs)"
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
