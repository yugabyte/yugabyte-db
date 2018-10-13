// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { PageHeader } from 'react-bootstrap';
import { change, Field } from 'redux-form';
import { YBButton, YBInputField, YBRadioButtonBarDefaultWithLabel } from '../fields';
import YBLogo from '../../YBLogo/YBLogo';
import {browserHistory} from 'react-router';
import {getPromiseState} from 'utils/PromiseUtils';

import './RegisterForm.scss';

class RegisterForm extends Component {
  componentWillReceiveProps(nextProps) {
    const {customer: {authToken}} =  nextProps;
    if (getPromiseState(authToken).isSuccess()) {
      browserHistory.push('/');
    }
  }

  submitRegister = formValues => {
    const {registerCustomer} = this.props;
    registerCustomer(formValues);
  };

  environmentChanged = value => {
    this.updateFormField('code', value);
  };


  updateFormField = (fieldName, fieldValue) => {
    this.props.dispatch(change("RegisterForm", fieldName, fieldValue));
  };

  render() {
    const { handleSubmit, submitting, customer: {authToken} } = this.props;

    return (
      <div className="container dark-background full-height page-register flex-vertical-middle">
        <div className="col-12 col-sm-10 col-md-8 col-lg-6 dark-form">
          <PageHeader bsClass="dark-form-heading">
            <YBLogo type="full"/>
            <span>Admin Console Registration</span>
          </PageHeader>
          <form className="form-register" onSubmit={handleSubmit(this.submitRegister.bind(this))}>
            <div className={`alert alert-danger form-error-alert ${authToken.error ? '': 'hide'}`}>
              {<strong>{JSON.stringify(authToken.error)}</strong>}
            </div>
            <div className="form-right-aligned-labels">
              <Field name="code" type="text" component={YBRadioButtonBarDefaultWithLabel} label="Environment"
                options={["dev", "demo", "stage", "prod"]} initialValue={"dev"} onSelect={this.environmentChanged} />
              <Field name="name" type="text" component={YBInputField} label="Full Name"/>
              <Field name="email" type="email" component={YBInputField} label="Email"/>
              <Field name="password" type="password" component={YBInputField} label="Password"/>
              <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password"/>
            </div>
            <div className="clearfix">
              <YBButton btnType="submit" btnDisabled={submitting || getPromiseState(authToken).isLoading()}
                        btnClass="btn btn-orange pull-right" btnText="Register"/>
            </div>
          </form>
        </div>
      </div>
    );
  }
}

export default RegisterForm;
