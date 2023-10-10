// Copyright (c) YugaByte, Inc.
//
// Author: Gaurav Raj(gauraraj@deloitte.com)
//
// This file will hold Universe alert creation and Platform
// alert creation.

import { Field, reduxForm, FieldArray } from 'redux-form';
import { useState, useEffect } from 'react';
import { Col, Row } from 'react-bootstrap';
import {
  YBButton,
  YBMultiSelectWithLabel,
  YBSelectWithLabel,
  YBTextArea,
  YBTextInputWithLabel,
  YBToggle
} from '../../common/forms/fields';
import { connect, useSelector } from 'react-redux';
import '../CreateAlerts.scss';
import { AlertsPolicy } from './AlertsPolicy';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { getAlertConfigByName } from '../../../actions/customers';
import { toast } from 'react-toastify';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { isNonAvailable } from '../../../utils/LayoutUtils';
import AlertPolicyDetails from './AlertPolicyDetails';

const required = (value) => (value ? undefined : 'This field is required.');

const CreateAlert = (props) => {
  const {
    customer,
    enablePlatformAlert,
    alertUniverseList,
    onCreateCancel,
    handleSubmit,
    initialValues,
    metricsData,
    setInitialValues,
    createAlertConfig,
    alertDestinations,
    updateAlertConfig
  } = props;

  const [isAllUniversesDisabled, setIsAllUniversesDisabled] = useState(
    initialValues.ALERT_TARGET_TYPE === 'allUniverses'
  );
  const [alertDestination, setAlertDestination] = useState([]);
  const [currentMetric, setCurrentMetric] = useState(undefined);
  const isReadOnly = isNonAvailable(customer.data.features, 'alert.configuration.actions');

  const featureFlags = useSelector((state) => state.featureFlags);

  useEffect(() => {
    alertDestinations().then((res) => {
      const defaultDestination = res.find((destination) => destination.defaultDestination);
      res = res.map((destination) => (
        <option key={destination.uuid} value={destination.uuid}>
          {destination.name}
        </option>
      ));
      setAlertDestination([
        <option key="default" value="<default>">
          Use Default ({defaultDestination?.name ?? 'No default destination configured'})
        </option>,
        <option key="empty" value="<empty>">
          No Destination
        </option>,
        ...res
      ]);
    });
  }, [alertDestinations]);

  useEffect(() => {
    setCurrentMetric((currentMetric) =>
      initialValues.ALERT_METRICS_CONDITION
        ? metricsData.find((metric) => metric.template === initialValues.ALERT_METRICS_CONDITION)
        : currentMetric
    );
  }, [metricsData, initialValues.ALERT_METRICS_CONDITION]);

  /**
   * Constant option for metrics condition.
   */
  const alertMetricsConditionList = [
    <option key="default" value={null}>
      Select Template
    </option>,
    ...metricsData.map((metric, i) => {
      return (
        // eslint-disable-next-line react/no-array-index-key
        <option key={i} value={metric.template}>
          {metric.name}
        </option>
      );
    })
  ];

  /**
   *
   * @param {Event} Event
   * Disable universe list dropdown and clear all the selection
   */
  const handleTargetTypeChange = (event) => {
    const value = event.target?.value;
    if (value === 'allUniverses') {
      setIsAllUniversesDisabled(true);
      props.updateField('alertConfigForm', 'ALERT_UNIVERSE_LIST', []);
    } else {
      setIsAllUniversesDisabled(false);
    }
  };

  /**
   * This method is used to set up the thresshold unit.
   *
   * @param {string} value Metric.
   */
  const handleMetricConditionChange = (value) => {
    const metric = metricsData.find((metric) => metric.template === value);

    setCurrentMetric(metric);
    if (!metric) {
      return;
    }
    const conditions = [];
    // Setting up the threshold values.
    Object.keys(metric.thresholds).forEach((policy) => {
      conditions.push({
        _SEVERITY: policy,
        _CONDITION: metric.thresholds[policy].condition,
        _THRESHOLD: metric.thresholds[policy].threshold
      });
    });
    props.updateField('alertConfigForm', 'ALERT_METRICS_CONDITION_POLICY', conditions);
    props.updateField('alertConfigForm', 'ALERT_METRICS_DURATION', metric.durationSec);
    props.updateField('alertConfigForm', 'ALERT_CONFIGURATION_NAME', metric.name);
    props.updateField('alertConfigForm', 'ALERT_CONFIGURATION_DESCRIPTION', metric.description);
    props.updateField('alertConfigForm', 'ALERT_STATUS', true);
  };

  /**
   *
   * @param {Formvalues} values
   * TODO: Make an API call to submit the form by reformatting the payload.
   */
  const handleOnSubmit = async (values) => {
    const cUUID = localStorage.getItem('customerId');
    if (
      values.type !== 'update' ||
      values['ALERT_CONFIGURATION_NAME'] !== initialValues['ALERT_CONFIGURATION_NAME']
    ) {
      const alertListByName = await getAlertConfigByName(values['ALERT_CONFIGURATION_NAME']);
      if (alertListByName.data.length !== 0) {
        toast.error(`Alert with name "${values['ALERT_CONFIGURATION_NAME']}" already exists!`);
        return;
      }
    }

    const payload = {
      uuid: values.type === 'update' ? values.uuid : null,
      customerUUID: cUUID,
      createTime: values.type === 'update' ? initialValues.createTime : null,
      name: values['ALERT_CONFIGURATION_NAME'],
      description: values['ALERT_CONFIGURATION_DESCRIPTION'],
      targetType: !enablePlatformAlert ? 'UNIVERSE' : 'PLATFORM',
      target: !enablePlatformAlert
        ? {
            all: isNonEmptyArray(values['ALERT_UNIVERSE_LIST']) ? false : true,
            uuids: isNonEmptyArray(values['ALERT_UNIVERSE_LIST']) ? [] : null
          }
        : { all: true },
      thresholds: '',
      thresholdUnit: currentMetric.thresholdUnit,
      template: values['ALERT_METRICS_CONDITION'] || 'REPLICATION_LAG',
      durationSec: values['ALERT_METRICS_DURATION'],
      active: values['ALERT_STATUS']
    };

    switch (values['ALERT_DESTINATION_LIST']) {
      case '<empty>':
        payload.destinationUUID = null;
        payload.defaultDestination = false;
        break;
      case '<default>':
        payload.destinationUUID = null;
        payload.defaultDestination = true;
        break;
      default:
        payload.destinationUUID = values['ALERT_DESTINATION_LIST'];
        payload.defaultDestination = false;
    }

    // Setting up the universe uuids.
    isNonEmptyArray(values['ALERT_UNIVERSE_LIST']) &&
      values['ALERT_UNIVERSE_LIST'].forEach((list) => payload.target.uuids.push(list.value));

    // Setting up the threshold values.
    isNonEmptyArray(values['ALERT_METRICS_CONDITION_POLICY']) &&
      values['ALERT_METRICS_CONDITION_POLICY'].forEach((policy) => {
        payload.thresholds = Object.assign(
          { [policy._SEVERITY]: { condition: policy._CONDITION, threshold: policy._THRESHOLD } },
          payload.thresholds
        );
      });

    values.type === 'update'
      ? updateAlertConfig(payload, values.uuid).then(() => onCreateCancel(false))
      : createAlertConfig(payload).then(() => onCreateCancel(false));
  };

  const targetOptions = [
    { label: 'All Universes', value: 'allUniverses' },
    { label: 'Selected Universes', value: 'selectedUniverses' }
  ];

  return (
    <form name="alertConfigForm" onSubmit={handleSubmit(handleOnSubmit)}>
      <Row className="config-section-header">
        <Col md={12}>
          <Row className="flex-container">
            <h4>New Alert Policy</h4>
            <Row className="component-flex">
              <Col lg={5}>
                <Field
                  name="ALERT_STATUS"
                  component={YBToggle}
                  isReadOnly={isReadOnly}
                  onChange={(event) =>
                    props.updateField('alertConfigForm', 'ALERT_STATUS', event?.target?.checked)
                  }
                />
              </Col>
              <Col lg={7} className="component-label mg-l-15 noPadding">
                <strong>Active</strong>
              </Col>
            </Row>
          </Row>
        </Col>
        <Row>
          <Col md={6}>
            <Field
              name="ALERT_METRICS_CONDITION"
              component={YBSelectWithLabel}
              validate={required}
              options={alertMetricsConditionList}
              onInputChanged={handleMetricConditionChange}
              readOnlySelect={isReadOnly}
              label="Policy Template"
            />
          </Col>
        </Row>
        {currentMetric && (
          <>
            <Row>
              <Col md={6}>
                <div className="form-item-custom-label">Name</div>
                <Field
                  name="ALERT_CONFIGURATION_NAME"
                  placeHolder="Enter an alert name"
                  component={YBTextInputWithLabel}
                  validate={required}
                  isReadOnly={isReadOnly}
                />
              </Col>
            </Row>
            <Row>
              <Col md={6}>
                <div className="form-item-custom-label">Description</div>
                <Field
                  name="ALERT_CONFIGURATION_DESCRIPTION"
                  placeHolder="Enter an alert description"
                  component={YBTextArea}
                  isReadOnly={isReadOnly}
                />
              </Col>
            </Row>
          </>
        )}
        {currentMetric && !enablePlatformAlert && (
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Target</div>
              {targetOptions.map((target) => (
                <label className="btn-group btn-group-radio" key={target.value}>
                  <Field
                    name="ALERT_TARGET_TYPE"
                    component="input"
                    onChange={handleTargetTypeChange}
                    type="radio"
                    disabled={isReadOnly}
                    value={target.value}
                  />{' '}
                  {target.label}
                </label>
              ))}
              <Field
                name="ALERT_UNIVERSE_LIST"
                component={YBMultiSelectWithLabel}
                options={alertUniverseList}
                hideSelectedOptions={false}
                isMulti={true}
                validate={!isAllUniversesDisabled && required}
                isReadOnly={isReadOnly}
                className={isAllUniversesDisabled ? 'hide-field' : ''}
              />
            </Col>
          </Row>
        )}
        {currentMetric && <hr />}
        {currentMetric && (
          <Row>
            <Col md={12}>
              <h4>Conditions</h4>
            </Col>
            <Col md={12}>
              <div className="form-item-custom-label">Duration</div>
            </Col>
            <div className="form-field-grid">
              <Col md={12}>
                <Row>
                  <Col lg={2}>
                    <Field
                      name="ALERT_METRICS_DURATION"
                      component={YBTextInputWithLabel}
                      validate={required}
                      placeHolder="Enter duration in minutes"
                      isReadOnly={isReadOnly}
                    />
                  </Col>
                  <Col lg={1}>
                    <div className="flex-container">
                      <p className="percent-text">sec</p>
                    </div>
                  </Col>
                </Row>
              </Col>
            </div>
            <Row>
              <Col md={12}>
                <div className="form-field-grid">
                  <FieldArray
                    name="ALERT_METRICS_CONDITION_POLICY"
                    component={AlertsPolicy}
                    props={{ currentMetric: currentMetric, isReadOnly: isReadOnly }}
                  />
                </div>
              </Col>
            </Row>
          </Row>
        )}
        {currentMetric && (
          <div className="actionBtnsMargin">
            <Row>
              <Col md={6}>
                <span className="form-item-custom-label marginRight">Alert Destination</span>
                <YBInfoTip
                  title="Destination"
                  content={`A destination consist of one or more channels. Whenever an Alert is triggered, it sends the related data to its designated destination.`}
                />
                <Field
                  name="ALERT_DESTINATION_LIST"
                  component={YBSelectWithLabel}
                  options={alertDestination}
                  readOnlySelect={isReadOnly}
                />
              </Col>
            </Row>
            {(featureFlags.test.enableCustomEmailTemplates ||
              featureFlags.released.enableCustomEmailTemplates) && (
              <Row>
                <Col md={6}>
                  <AlertPolicyDetails currentMetric={currentMetric} />
                </Col>
              </Row>
            )}
          </div>
        )}

        <Row className="alert-action-button-container">
          <Col lg={6} lgOffset={6}>
            <YBButton
              btnText="Cancel"
              btnClass="btn"
              onClick={() => {
                onCreateCancel(false);
                setInitialValues();
              }}
            />
            {currentMetric && !isReadOnly && (
              <YBButton btnText="Save" btnType="submit" btnClass="btn btn-orange" />
            )}
          </Col>
        </Row>
      </Row>
    </form>
  );
};

const mapStateToProps = (state, ownProps) => {
  return {
    initialValues: { ...ownProps.initialValues }
  };
};

export default connect(mapStateToProps)(
  reduxForm({
    form: 'alertConfigForm',
    enableReinitialize: true,
    keepDirtyOnReinitialize: true
  })(CreateAlert)
);
