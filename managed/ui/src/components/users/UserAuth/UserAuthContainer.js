// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { UserAuth } from './UserAuth';
import {
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  deleteRunTimeConfig,
  deleteRunTimeConfigResponse,
  setRunTimeConfig,
  setRunTimeConfigResponse,
  DEFAULT_RUNTIME_GLOBAL_SCOPE
} from '../../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchRunTimeConfigs: () => {
      return dispatch(fetchRunTimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)).then((response) =>
        dispatch(fetchRunTimeConfigsResponse(response.payload))
      );
    },
    setRunTimeConfig: (payload) => {
      return dispatch(setRunTimeConfig(payload)).then((response) =>
        dispatch(setRunTimeConfigResponse(response.payload))
      );
    },
    deleteRunTimeConfig: (payload) => {
      return dispatch(deleteRunTimeConfig(payload)).then((response) =>
        dispatch(deleteRunTimeConfigResponse(response.payload))
      );
    }
  };
};

function mapStateToProps(state) {
  return {
    runtimeConfigs: state.customer.runtimeConfigs,
    featureFlags: state.featureFlags
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UserAuth);
