import { useEffect, useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { Field, reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import {
  YBButton,
  YBCheckBox,
  YBMultiSelectWithLabel,
  YBTextInputWithLabel
} from '../../common/forms/fields';
import { AddDestinationChannelForm } from './AddDestinationChannelForm';
import { isNonAvailable } from '../../../utils/LayoutUtils';

import './AlertDestinationConfiguration.scss';

const required = (value) => (value ? undefined : 'This field is required.');

const AlertDestinationConfiguration = (props) => {
  const [destinationChannelList, setDestinationChannelList] = useState([]);

  const onInit = () => {
    props.getAlertChannels().then((channels) => {
      channels = channels.map((channel) => {
        return {
          value: channel['uuid'],
          label: channel['name']
        };
      });
      setDestinationChannelList(channels);
    });
  };

  useEffect(onInit, []);

  /**
   *
   * @param {Formvalues} values
   * TODO: Make an API call to submit the form by reformatting the payload.
   */
  const handleOnSubmit = (values) => {
    const payload = {
      name: '',
      channels: [],
      defaultDestination: values['defaultDestination'] ? values['defaultDestination'] : false
    };
    payload.name = values['ALERT_DESTINATION_NAME'];
    values['DESTINATION_CHANNEL_LIST'].forEach((channel) => payload.channels.push(channel.value));

    values.type === 'update'
      ? props.updateAlertDestination(payload, values.uuid).then((response) => {
          const status = response?.payload?.response?.status || response?.payload?.status;
          if (status === 200 || status === 201) {
            props.setInitialValues();
            props.onAddCancel();
          }
        })
      : props.createAlertDestination(payload).then((response) => {
          const status = response?.payload?.response?.status || response?.payload?.status;
          if (status === 200 || status === 201) {
            props.setInitialValues();
            props.onAddCancel();
          }
        });
  };

  const {
    customer,
    handleSubmit,
    onAddCancel,
    setInitialValues,
    modal: { showModal, visibleModal }
  } = props;
  const isReadOnly = isNonAvailable(customer.data.features, 'alert.destinations.actions');

  return (
    <>
      <form name="alertDestinationForm" onSubmit={handleSubmit(handleOnSubmit)}>
        <Row className="config-section-header">
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Destination Name</div>
              <Field
                name="ALERT_DESTINATION_NAME"
                placeHolder="Enter an alert destination"
                component={YBTextInputWithLabel}
                validate={required}
                isReadOnly={isReadOnly}
              />
            </Col>
          </Row>
          <Row>
            <Col md={6} className="make-destination-label">
              <Field
                name="defaultDestination"
                component={YBCheckBox}
                disabled={isReadOnly}
                checkState={props.initialValues.defaultDestination ? true : false}
                label="Make this destination as the default destination"
              />
            </Col>
          </Row>
          <Row>
            <Col md={6}>
              <div className="form-item-custom-label">Choose Channels</div>
              <Field
                name="DESTINATION_CHANNEL_LIST"
                component={YBMultiSelectWithLabel}
                options={destinationChannelList}
                hideSelectedOptions={false}
                isReadOnly={isReadOnly}
                isMulti={true}
                validate={required}
              />
            </Col>
            {!isReadOnly && (
              <Col md={6} className="add-destination-container">
                <Row>
                  <Col lg={3} className="pd-0">
                    <a href="# " className="on-prem-add-link" onClick={props.showAddChannelModal}>
                      <i
                        className="fa fa-plus-circle fa-2x on-prem-row-add-btn"
                        onClick={props.showAddChannelModal}
                      />
                      Add Channel
                    </a>
                  </Col>
                </Row>
              </Col>
            )}
          </Row>
          <Row className="alert-action-button-container">
            <Col lg={6} lgOffset={6}>
              <YBButton
                btnText="Cancel"
                btnClass="btn"
                onClick={() => {
                  setInitialValues();
                  onAddCancel(false);
                }}
              />
              {!isReadOnly && (
                <YBButton btnText="Save" btnType="submit" btnClass="btn btn-orange" />
              )}
            </Col>
          </Row>
        </Row>
      </form>
      <AddDestinationChannelForm
        visible={showModal && visibleModal === 'alertDestinationForm'}
        onHide={props.closeModal}
        defaultChannel="email"
        updateDestinationChannel={setDestinationChannelList}
        {...props}
      />
    </>
  );
};

const mapStateToProps = (state, ownProps) => {
  return {
    initialValues: { ...ownProps.initialValues }
  };
};

export default connect(mapStateToProps)(
  reduxForm({
    form: 'alertDestinationForm',
    enableReinitialize: true
  })(AlertDestinationConfiguration)
);
