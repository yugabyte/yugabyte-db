// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, Formik } from 'formik';
import * as Yup from "yup";
import { connect } from 'react-redux';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { YBModal, YBFormSelect, YBFormInput } from '../common/forms/fields';
import {
  getCustomerUsers,
  getCustomerUsersSuccess,
  getCustomerUsersFailure,
  createUser,
  createUserResponse
} from '../../actions/customers';
import { isNonEmptyObject } from '../../utils/ObjectUtils';

class AddUserModal extends Component {
  handleSubmitForm = (values) => {
    values.role = isNonEmptyObject(values.role) ?
        values.role.value : values.role;
    this.props.createUser(values);
    this.props.onHide();
    this.props.getCustomerUsers();
  };

  render() {
    const { modalVisible, onHide } = this.props;

    const initialValues = {
      'email': "",
      'password': "",
      'confirmPassword': "",
      'role': "ReadOnly"
    };

    const validationSchema = Yup.object().shape({
      email: Yup.string().email("Enter a valid email"),
      password: Yup.string().required('Password is required'),
      confirmPassword: Yup.string()
        .oneOf([Yup.ref('password'), null], 'Passwords must match'),
      role: Yup.string()
    });

    const userRoles = [{value: 'ReadOnly', label: 'ReadOnly'},
      {value: 'Admin', label: 'Admin'}
    ];
    return (
      <Formik
        initialValues={initialValues}
        validationSchema={validationSchema}
        onSubmit={(values) => {
          this.handleSubmitForm(values);
        }}
        render={props => (
          <YBModal visible={modalVisible} formName={"EncryptionForm"} onHide={onHide}
            onFormSubmit={props.handleSubmit} submitLabel={'Submit'} cancelLabel={'Close'}
            showCancelButton={true} title={ "Add User" }
          >
            <div className="add-user-container">
              <Row className="config-provider-row">
                <Col lg={3}>
                  <div className="form-item-custom-label">Email</div>
                </Col>
                <Col lg={7}>
                  <Field name="email" placeholder="Email address"
                         component={YBFormInput}/>
                </Col>
              </Row>
              <Row className="config-provider-row">
                <Col lg={3}>
                  <div className="form-item-custom-label">Password</div>
                </Col>
                <Col lg={7}>
                  <Field name="password" placeholder="Password"
                         type="password" component={YBFormInput} />
                </Col>
              </Row>
              <Row className="config-provider-row">
                <Col lg={3}>
                  <div className="form-item-custom-label">Confirm Password</div>
                </Col>
                <Col lg={7}>
                  <Field name="confirmPassword" placeholder="Confirm Password"
                         type="password" component={YBFormInput} />
                </Col>
              </Row>
              <Row className="config-provider-row">
                <Col lg={3}>
                  <div className="form-item-custom-label">Role</div>
                </Col>
                <Col lg={7}>
                  <Field name="role" component={YBFormSelect} options={userRoles}/>
                </Col>
              </Row>
            </div>
          </YBModal>
        )} />
    );
  }
}


const mapDispatchToProps = (dispatch) => {
  return {
    getCustomerUsers: () => {
      dispatch(getCustomerUsers()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getCustomerUsersFailure(response.payload));
        } else {
          dispatch(getCustomerUsersSuccess(response.payload));
        }
      });
    },
    createUser: (payload) => {
      dispatch(createUser(payload)).then((response) => {
        dispatch(createUserResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    customer: state.customer,
    modal: state.modal
  };
}


export default connect(mapStateToProps, mapDispatchToProps)(AddUserModal);
