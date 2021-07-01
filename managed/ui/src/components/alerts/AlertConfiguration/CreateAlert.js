// Copyright (c) YugaByte, Inc.
//
// Author: Gaurav Raj(gauraraj@deloitte.com)
//
// This file will hold Universe alert creation and Platform
// alert creation.
// TODO: Platform alert creation.

import { Field, reduxForm, FieldArray } from 'redux-form';
import React, { useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import {
  YBButton,
  YBFormInput,
  YBMultiSelectWithLabel,
  YBRadioButtonGroup,
  YBSelectWithLabel,
  YBTextArea,
  YBTextInputWithLabel
} from '../../common/forms/fields';
import { Formik } from 'formik';
import '../CreateAlerts.scss';
import { useSelector } from 'react-redux';
import AlertsPolicy from './AlertsPolicy';

const required = (value) => (value ? undefined : 'This field is required.');

const CreateAlert = (props) => {
  const { onCreateCancel, handleSubmit } = props;
  const [isAllUniversesDisabled, setIsAllUniversesDisabled] = useState(true);
  const universes = useSelector((state) => {});

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
   * Constant option for universe list
   * TODO: Get the list of universe from API
   */
  const alertUniverseList = [
    { value: 'puppy-food-4s-1', label: 'puppy-food-4s-1' },
    { value: 'yugabye-adoption-1', label: 'yugabye-adoption-1' }
  ];

  /**
   * Constant option for alert destination list
   * TODO: Get the list of universe from API
   */
  const alertDestinationList = [
    <option key={1} value={'Configured Email destination 1'}>
      {'Configured Email destination 1'}
    </option>,
    <option key={1} value={'Configured Email destination 2'}>
      {'Configured Email destination 2'}
    </option>,
    <option key={1} value={'Configured Slack destination 2'}>
      {'Configured Slack destination 2'}
    </option>,
    <option key={1} value={'Configured Slack destination 1'}>
      {'Configured Slack destination 1'}
    </option>
  ];

  /**
   *
   * @param {Event} event
   * TODO: Change the state to disable/enable the universe multi-select list.
   */
  const handleMetricConditionChange = (event) => {
    const value = event.target?.value;
    value === 'allCluster' ? setIsAllUniversesDisabled(true) : setIsAllUniversesDisabled(false);
  };
  /**
   *
   * @param {Formvalues} values
   * TODO: Make an API call to submit the form by reformatting the payload.
   */
  const handleOnSubmit = (values) => {
    // console.log(values)
  };
  return (
    <Formik initialValues={{ ALERT_TARGET_TYPE: 'allCluster' }}>
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
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Target</div>
              <YBRadioButtonGroup
                name={'ALERT_TARGET_TYPE'}
                options={[
                  { label: 'All Cluster', value: 'allCluster' },
                  { label: 'Selected Cluster', value: 'selectedCluster' }
                ]}
                onClick={handleMetricConditionChange}
              />
              <Field
                name="ALERT_UNIVERSE_LIST"
                component={YBMultiSelectWithLabel}
                options={alertUniverseList}
                hideSelectedOptions={false}
                data-yb-field="regions"
                isMulti={true}
                isDisabled={isAllUniversesDisabled}
                // selectValChanged={handleMetricConditionChange}
                providerSelected={'puppy-food-4s-1'}
              />
            </Col>
          </Row>
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
                <Field
                  component={YBFormInput}
                  type="number"
                  label="Duration"
                  placeholder="Enter duration in minutes"
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
                options={alertDestinationList}
              />
            </Col>
          </Row>
          <Row className="form-action-button-container">
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
