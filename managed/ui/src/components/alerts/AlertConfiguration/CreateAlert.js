// Copyright (c) YugaByte, Inc.
//
// Author: Gaurav Raj(gauraraj@deloitte.com)
//
// This file will hold Universe alert creation and Platform
// alert creation.
// TODO: Platform alert creation.

import { Field, reduxForm, FieldArray } from 'redux-form';
import React, { useState, useEffect } from 'react';
import { Col, Row } from 'react-bootstrap';
import {
  YBButton,
  YBMultiSelectWithLabel,
  YBRadioButtonGroup,
  YBSelectWithLabel,
  YBTextArea,
  YBTextInputWithLabel
} from '../../common/forms/fields';
import { Formik } from 'formik';
import '../CreateAlerts.scss';
import AlertsPolicy from './AlertsPolicy';

const required = (value) => (value ? undefined : 'This field is required.');

const CreateAlert = (props) => {
  const {
    enablePlatformAlert,
    onCreateCancel,
    handleSubmit,
    alertDestionations,
    universes
  } = props;
  const [isAllUniversesDisabled, setIsAllUniversesDisabled] = useState(true);
  const [alertDestionation, setAlertDesionation] = useState([]);
  const [alertUniverseList, setAlertUniverseList] = useState([]);

  useEffect(() => {
    alertDestionations().then((res) => {
      res = res.map((destination) => (
        <option key={1} value={destination.uuid}>
          {destination.name}
        </option>
      ));
      setAlertDesionation(res);
    });

    setAlertUniverseList([
      ...universes.data.map((universe) => ({ label: universe.name, value: universe.universeUUID }))
    ]);
  }, []);

  /**
   * Constant option for metrics condition
   * TODO: Source and values of actual list may differ.
   */
  const alertMetricsConditionList = [
    <option key={1} value={'cpuUtilization'}>
      {'CPU Utiliztion'}
    </option>,
    <option key={2} value={'memoryUtilization'}>
      {'Memory Utilization'}
    </option>
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
   *
   * @param {Formvalues} values
   * TODO: Make an API call to submit the form by reformatting the payload.
   */
  const handleOnSubmit = (values) => {
    console.log(values);
  };
  return (
    <Formik initialValues={{ ALERT_TARGET_TYPE: 'allUniverses' }}>
      <form name="alertConfigForm" onSubmit={handleSubmit(handleOnSubmit)}>
        <Row className="config-section-header">
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Name</div>
              <Field
                name="ALERT_CONFIGURATION_NAME"
                placeHolder="Enter an alert name"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={false}
              />
            </Col>
            <Col md={6}>
              <div className="form-item-custom-label">Description</div>
              <Field
                name="ALERT_CONFIGURATION_DESCRIPTION"
                placeHolder="Enter an alert description"
                component={YBTextArea}
                validate={required}
                isReadOnly={false}
              />
            </Col>
          </Row>
          {!enablePlatformAlert && (
            <Row>
              <Col md={6}>
                <div className="form-item-custom-label">Target</div>
                <YBRadioButtonGroup
                  name={'ALERT_TARGET_TYPE'}
                  options={[
                    { label: 'All Universes', value: 'allUniverses' },
                    { label: 'Selected Universes', value: 'selectedUniverses' }
                  ]}
                  onClick={handleTargetTypeChange}
                />
                <Field
                  name="ALERT_UNIVERSE_LIST"
                  component={YBMultiSelectWithLabel}
                  options={alertUniverseList}
                  hideSelectedOptions={false}
                  isMulti={true}
                  isDisabled={isAllUniversesDisabled}
                />
              </Col>
            </Row>
          )}
          <hr />
          <Row>
            <Col md={12}>
              <h4>Conditions</h4>
            </Col>
            <Row>
              <Col md={6}>
                <div className="form-item-custom-label">Metrics</div>
                <Field
                  name="ALERT_METRICS_CONDITION"
                  component={YBSelectWithLabel}
                  options={alertMetricsConditionList}
                  onInputChanged={() => {}}
                />
              </Col>
              <Col md={6}>
                <div className="form-item-custom-label">Duration</div>
                <Field
                  name="ALERT_METRICS_DURATION"
                  component={YBTextInputWithLabel}
                  validate={required}
                  placeHolder="Enter duration in minutes"
                />
              </Col>
            </Row>
            <Row>
              <Col md={12}>
                <div className="form-field-grid">
                  <FieldArray name="ALERT_METRICS_CONDITION_POLICY" component={AlertsPolicy} />
                </div>
              </Col>
            </Row>
          </Row>
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Destinations</div>
              <Field
                name="ALERT_DESTINATION_LIST"
                component={YBSelectWithLabel}
                options={alertDestionation}
              />
            </Col>
          </Row>
          <Row className="alert-action-button-container">
            <Col lg={6} lgOffset={6}>
              <YBButton
                btnText="Cancel"
                btnClass="btn"
                onClick={() => {
                  onCreateCancel(false);
                }}
              />
              <YBButton btnText="Save" btnType="submit" btnClass="btn btn-orange" />
            </Col>
          </Row>
        </Row>
      </form>
    </Formik>
  );
};

export default reduxForm({
  form: 'alertConfigForm',
  enableReinitialize: true
})(CreateAlert);
