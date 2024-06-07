// Copyright (c) YugaByte, Inc.

import { Component } from 'react';

import RegisterFormContainer from '../components/common/forms/RegisterForm/RegisterFormContainer';

class Register extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <RegisterFormContainer />
      </div>
    );
  }
}

export default Register;
