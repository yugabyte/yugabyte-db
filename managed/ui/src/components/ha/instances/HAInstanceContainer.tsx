import axios from 'axios';
import { connect } from 'react-redux';
import { toast } from 'react-toastify';

import {
  DEFAULT_RUNTIME_GLOBAL_SCOPE,
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  setRunTimeConfig,
  setRunTimeConfigResponse
} from '../../../actions/customers';
import { HAInstances } from './HAInstances';

const SET_RUNTIME_CONFIG_ERROR_MESSAGE = 'Encountered an error while trying to set runtime config';

const mapDispatchToProps = (dispatch: any) => {
  return {
    fetchRuntimeConfigs: () => {
      dispatch(fetchRunTimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)).then((response: any) =>
        dispatch(fetchRunTimeConfigsResponse(response.payload))
      );
    },
    setRuntimeConfig: (key: string, value: string) => {
      dispatch(
        setRunTimeConfig({
          key: key,
          value: value
        })
      ).then((response: any) => {
        const payload = response?.payload;
        try {
          if (axios.isAxiosError(payload)) {
            toast.error(`${SET_RUNTIME_CONFIG_ERROR_MESSAGE}: ${payload.message}`);
          } else if (payload === undefined || payload === null) {
            toast.error(`${SET_RUNTIME_CONFIG_ERROR_MESSAGE}.`);
          } else {
            dispatch(setRunTimeConfigResponse(response.payload));
          }
        } catch (error) {
          toast.error(`${SET_RUNTIME_CONFIG_ERROR_MESSAGE}.`);
        } finally {
          dispatch(fetchRunTimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)).then((response: any) =>
            dispatch(fetchRunTimeConfigsResponse(response.payload))
          );
        }
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

export const HAInstancesContainer = connect(mapStateToProps, mapDispatchToProps)(HAInstances);
