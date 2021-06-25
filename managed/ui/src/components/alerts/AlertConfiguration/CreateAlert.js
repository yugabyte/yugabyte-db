// Copyright (c) YugaByte, Inc.
//
// Author: Gaurav Raj(gauraraj@deloitte.com)
//
// This file will hold Universe alert creation and Platform
// alert creation.
// TODO: Platform alert creation.

import { change, Field, reduxForm, FieldArray } from 'redux-form';
import React from 'react';
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
import { useDispatch, useSelector } from 'react-redux';
import AlertsPolicy from './AlertsPolicy';

const required = (value) => (value ? undefined : 'This field is required.');

const CreateAlert = (props) => {
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
    { value: 'Configured Email destination 1', label: 'Configured Email destination 1' },
    { value: 'Configured Slack destination 2', label: 'Configured Slack destination 2' },
    { value: 'Configured Email destination 2', label: 'Configured Email destination 2' },
    { value: 'Configured Slack destination 1', label: 'Configured Slack destination 1' }
  ];

  /**
   * 
   * @param {Event} event 
   * TODO: Change the state to disable/enable the universe multi-select list.
   */
  const handleMetricConditionChange = (event) => {
    console.log('val', event.target.value)
  };
  /**
   * 
   * @param {Formvalues} values 
   * TODO: Make an API call to submit the form by reformatting the payload.
   */
  const handleOnSubmit = (values) => {
    // console.log(values)
  }
  return (
    <Formik initialValues={{ ALERT_TARGET_TYPE: 'allCluster' }}>
      <form name="alertConfigForm" onSubmit={props.handleSubmit(handleOnSubmit)}>
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
                selectValChanged={(val) => {
                  console.log(val);
                }}
                providerSelected={'puppy-food-4s-1'}
              />
            </Col>
          </Row>
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
                  onInputChanged={handleMetricConditionChange}
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
              {/* isDisabled={true} */}
              <div className="form-item-custom-label">Destinations</div>
              <Field
                name="ALERT_DESTINATION_LIST"
                component={YBMultiSelectWithLabel}
                options={alertDestinationList}
                hideSelectedOptions={false}
                data-yb-field="regions"
                isMulti={true}
                selectValChanged={(val) => {
                  console.log(val);
                }}
                providerSelected={'puppy-food-4s-1'}
              />
            </Col>
          </Row>
          <Row className="form-action-button-container">
            <Col lg={4} lgOffset={8}>
              <YBButton btnText="Cancel" btnClass="btn" onClick={() => {}} />
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
  enableReinitialize: true,
  fields: [
    'ALERT_CONFIGURATION_NAME',
    'ALERT_CONFIGURATION_DESCRIPTION',
    'ALERT_TARGET_TYPE',
    'ALERT_UNIVERSE_LIST',
    'ALERT_METRICS_CONDITION'
  ]
})(CreateAlert);
