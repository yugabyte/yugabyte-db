// Copyright (c) YugaByte, Inc.

import RegisterForm from './RegisterForm';
import { register, registerResponse, 
  fetchPasswordPolicy, 
  fetchPasswordPolicyResponse,
  addCustomerConfig,
  addCustomerConfigResponse
} from '../../../../actions/customers';
import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {
    addCustomerConfig: (config) => {
      dispatch(addCustomerConfig(config)).then((response) => {
        if (!response.error) {
          dispatch(addCustomerConfigResponse(response.payload));
        }
      });
    },
    registerCustomer: (formVals) => {
      dispatch(register(formVals)).then((response) => {
        if (response.payload.status === 200) {
          localStorage.setItem('authToken', response.payload.data.authToken);
          localStorage.setItem('customerId', response.payload.data.customerUUID);
        }
        dispatch(registerResponse(response.payload));
      });
    },
    validateRegistration: () => {
      dispatch(fetchPasswordPolicy()).then((response) => {
        if (response.payload.status === 200) {
          dispatch(fetchPasswordPolicyResponse(response.payload));
        }
      });
    },
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer,
    validateFields: state.validateFields,
    initialValues: { code: 'dev' },
    passwordValidationInfo: state.customer.passwordValidationInfo
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(RegisterForm);
