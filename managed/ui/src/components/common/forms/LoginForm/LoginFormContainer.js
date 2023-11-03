// Copyright (c) YugaByte, Inc.

import LoginForm from './LoginForm';
import {
  login,
  loginResponse,
  resetCustomerError,
  fetchYugaWareVersion,
  fetchYugaWareVersionResponse,
  validateToken
} from '../../../../actions/customers';
import { connect } from 'react-redux';
import { api } from '../../../../redesign/features/universe/universe-form/utils/api';
import { RBAC_RUNTIME_FLAG, setIsRbacEnabled } from '../../../../redesign/features/rbac/common/RbacUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    getYugaWareVersion: () => {
      dispatch(fetchYugaWareVersion()).then((response) => {
        dispatch(fetchYugaWareVersionResponse(response.payload));
      });
    },

    loginCustomer: (formValues) => {
      dispatch(login(formValues)).then((response) => {
        if (response.payload.status === 200) {
          const { authToken, customerUUID, userUUID } = response.payload.data;
          localStorage.setItem('authToken', authToken);
          localStorage.setItem('customerId', customerUUID);
          localStorage.setItem('userId', userUUID);
        }
        dispatch(loginResponse(response.payload));
      });
    },
    resetCustomerError: () => dispatch(resetCustomerError())
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(LoginForm);
