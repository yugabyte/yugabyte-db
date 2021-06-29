import { Field, Formik } from 'formik';
import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { reduxForm } from 'redux-form';
import { YBMultiSelectWithLabel, YBTextInputWithLabel } from '../../common/forms/fields';

const required = (value) => (value ? undefined : 'This field is required.');

const AlertDestinationConfiguration = (props) => {
  /**
   * Constant value of channel list.
   */
  const destinationChannelList = [
    { value: 'Configured slack channel', label: 'Configured slack channel' },
    { value: 'Configured email channel', label: 'Configured email channel' },
    { value: 'Configured pagerDuty channel', label: 'Configured pagerDuty channel' }
  ];
    /**
   *
   * @param {Formvalues} values
   * TODO: Make an API call to submit the form by reformatting the payload.
   */
     const handleOnSubmit = (values) => {
      // console.log(values)
    };
  return (
    <Formik initialValues={null}>
      <form name="alertDestinationForm" onSubmit={props.handleSubmit(handleOnSubmit)}>
        <Row className="config-section-header">
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Destination Name</div>
              <Field
                name="ALERT_DESTINATION_NAME"
                placeHolder="Enter an alert destination"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={false}
              />
            </Col>
          </Row>
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Choose Channels</div>
              <Field
                name="DESTINATION_CHANNEL_LIST"
                placeHolder="Select one or more Channnels"
                component={YBMultiSelectWithLabel}
                options={destinationChannelList}
                hideSelectedOptions={false}
                isMulti={true}
              />
            </Col>
            <Col md={3}>
              <a className="on-prem-add-link" onClick={() => {}}>
                Add Severity{' '}
              </a>
            </Col>
          </Row>
        </Row>
      </form>
    </Formik>
  );
};

export default reduxForm({
  form: 'alertDestinationForm',
  enableReinitialize: true
})(AlertDestinationConfiguration);
