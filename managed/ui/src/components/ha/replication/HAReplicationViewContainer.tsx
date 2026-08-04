import { connect } from 'react-redux';
import {
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  setRunTimeConfig,
  setRunTimeConfigResponse
} from '../../../actions/customers';

import { HAReplicationView } from './HAReplicationView';

const mapDispatchToProps = (dispatch: any) => {
  return {
    fetchRuntimeConfigs: () => {
      dispatch(
        fetchRunTimeConfigs('00000000-0000-0000-0000-000000000000', true)
      ).then((response: any) => dispatch(fetchRunTimeConfigsResponse(response.payload)));
    },
    setRuntimeConfig: (key: string, value: string) => {
      dispatch(
        setRunTimeConfig({
          key: key,
          value: value
        })
      ).then((response: any) => {
        dispatch(setRunTimeConfigResponse(response.payload));
        dispatch(
          fetchRunTimeConfigs('00000000-0000-0000-0000-000000000000', true)
        ).then((response: any) => dispatch(fetchRunTimeConfigsResponse(response.payload)));
      });
    }
  };
};

const mapStateToProps = (state: any) => {
  const { runtimeConfigs } = state.customer;

  return {
    runtimeConfigs: runtimeConfigs
  };
};

export const HAReplicationViewContainer = connect(
  mapStateToProps,
  mapDispatchToProps
)(HAReplicationView);
