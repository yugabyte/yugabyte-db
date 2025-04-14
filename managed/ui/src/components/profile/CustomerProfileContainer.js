// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { CustomerProfile } from '../profile';
import {
  addCustomerConfig,
  addCustomerConfigResponse,
  updateProfile,
  updateProfileSuccess,
  updateProfileFailure,
  getApiToken,
  getApiTokenResponse,
  getApiTokenLoading,
  getCustomerUsers,
  getCustomerUsersSuccess,
  getCustomerUsersFailure,
  fetchPasswordPolicy,
  fetchPasswordPolicyResponse,
  fetchOIDCToken,
  fetchOIDCTokenResponse,
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  updateUserProfile,
  updateUserProfileFailure,
  updateUserProfileSuccess,
  updatePassword,
  DEFAULT_RUNTIME_GLOBAL_SCOPE
} from '../../actions/customers';
import { toast } from 'react-toastify';

const mapDispatchToProps = (dispatch) => {
  return {
    getCustomerUsers: () => {
      dispatch(getCustomerUsers()).then((response) => {
        try {
          if (response.payload.status !== 200) {
            dispatch(getCustomerUsersFailure(response.payload));
          } else {
            dispatch(getCustomerUsersSuccess(response.payload));
          }
        } catch (error) {
          console.error('Error while fetching customer users');
        }
      });
    },
    fetchOIDCToken: (userUUID) => {
      dispatch(fetchOIDCToken(userUUID)).then((response) => {
        dispatch(fetchOIDCTokenResponse(response.payload));
      });
    },
    fetchGlobalRunTimeConfigs: () => {
      return dispatch(fetchRunTimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)).then((response) =>
        dispatch(fetchRunTimeConfigsResponse(response.payload))
      );
    },
    updateCustomerDetails: (values) => {
      dispatch(updateProfile(values)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(updateProfileFailure(response.payload));
        } else {
          dispatch(updateProfileSuccess(response.payload));
        }
      });
    },
    updateUserProfile: (userUUID, values) => {
      dispatch(updateUserProfile(userUUID, values)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(updateUserProfileFailure(response.payload));
        } else {
          // When user updates the timezone, we need to update the timezone in sessionStorage
          // so that metrics page can use the updated timezone
          sessionStorage.setItem('metricsTimezone', values.timezone);
          dispatch(updateUserProfileSuccess(response.payload));
        }
      });
    },
    updateUserPassword: (userUUID, values) => {
      return updatePassword(userUUID, values)
        .then(() => {
          toast.success('Password updated successfully');
        })
        .catch((payload) => {
          toast.error(payload.response.data.error);
        });
    },
    addCustomerConfig: (config) => {
      dispatch(addCustomerConfig(config)).then((response) => {
        if (!response.error) {
          dispatch(addCustomerConfigResponse(response.payload));
        }
      });
    },
    validateRegistration: () => {
      dispatch(fetchPasswordPolicy()).then((response) => {
        if (response.payload.status === 200) {
          dispatch(fetchPasswordPolicyResponse(response.payload));
        }
      });
    },
    refreshApiToken: (authToken) => {
      dispatch(getApiTokenLoading());
      dispatch(getApiToken(authToken)).then((response) => {
        if (response.payload.status === 200) {
          response.payload.data = response.payload.data.apiToken;
        }
        dispatch(getApiTokenResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer.currentCustomer,
    runtimeConfigs: state.customer.runtimeConfigs,
    currentUser: state.customer.currentUser,
    users: state.customer.users.data,
    apiToken: state.customer.apiToken,
    OIDCToken: state.customer.OIDCToken,
    customerProfile: state.customer ? state.customer.profile : null,
    passwordValidationInfo: state.customer.passwordValidationInfo
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(CustomerProfile);
