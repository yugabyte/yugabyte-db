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
          validateToken();
          api.fetchRunTimeConfigs().then((res) => {
            const rbacKey = res.configEntries?.find(
              (c) => c.key === RBAC_RUNTIME_FLAG
            );
            setIsRbacEnabled(rbacKey?.value === 'true');
            dispatch(loginResponse(response.payload));
          }).catch((res) => {
            //Rbac is enabled and the user is connect only user
            if (res.response.status === 401) {
              setIsRbacEnabled(true);
              dispatch(loginResponse(response.payload));
            }
            else {
              setIsRbacEnabled(false);
              dispatch(loginResponse(response.payload));
            }
          });
        }
        else {
          dispatch(loginResponse(response.payload));
        }
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
