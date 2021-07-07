// Copyright (c) YugaByte, Inc.

import RegisterForm from './RegisterForm';
import { register, registerResponse, 
  validate, 
  validateResponse 
} from '../../../../actions/customers';
import { connect } from 'react-redux';

const mapDispatchToProps = (dispatch) => {
  return {
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
      dispatch(validate()).then((response) => {
        if (response.payload.status === 200) {
          dispatch(validateResponse(response.payload));
        }
      });
    },
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer,
    validateFields: state.validateFields,
    initialValues: { code: 'dev' }
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(RegisterForm);
