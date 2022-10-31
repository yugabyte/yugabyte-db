import { connect } from 'react-redux';
import { toast } from 'react-toastify';

import {
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  setRunTimeConfig,
  setRunTimeConfigResponse,
  deleteRunTimeConfig,
  deleteRunTimeConfigResponse,
  resetRuntimeConfigs,
  DEFAULT_RUNTIME_GLOBAL_SCOPE
} from '../../../actions/customers';
import { RuntimeConfig } from './RuntimeConfig';

const mapDispatchToProps = (dispatch: any) => {
  return {
    fetchRuntimeConfigs: (scope: string = DEFAULT_RUNTIME_GLOBAL_SCOPE) => {
      dispatch(
        fetchRunTimeConfigs(scope, true)
      ).then((response: any) => dispatch(fetchRunTimeConfigsResponse(response.payload)));
    },
    setRuntimeConfig: (key: string, value: string, scope: string = DEFAULT_RUNTIME_GLOBAL_SCOPE) => {
      dispatch(
        setRunTimeConfig({
          key: key,
          value: value,
          scope
        })
      ).then((response: any) => {
        try {
          if (response.payload.isAxiosError || response.payload.status !== 200) {
            toast.error("Could not save config, try again");
          } else {
            toast.success("Config saved successfully");
            dispatch(setRunTimeConfigResponse(response.payload));
          }
        } catch (error) {
          console.error('Error while trying to save config');
        } finally {
          dispatch(
            fetchRunTimeConfigs(scope, true)
          ).then((response: any) => dispatch(fetchRunTimeConfigsResponse(response.payload)));
        }
      });
    },
    deleteRunTimeConfig: (key: string, scope: string = DEFAULT_RUNTIME_GLOBAL_SCOPE) => {
      dispatch(
        deleteRunTimeConfig({
          key: key,
          scope
        })).then((response: any) => {
          try {
            if (response.payload.isAxiosError || response.payload.status !== 200) {
              toast.error("Could not delete config, try again");
            } else {
              toast.success("Config resetted to parent scope successfully");
              dispatch(deleteRunTimeConfigResponse(response.payload))
            }
          } catch (error) {
            console.error('Error while trying to save config');
          } finally {
            dispatch(
              fetchRunTimeConfigs(scope, true)
            ).then((response: any) => dispatch(fetchRunTimeConfigsResponse(response.payload)));
          }
        });
    },
    resetRuntimeConfigs: () => {
      dispatch(resetRuntimeConfigs());
    }
  };
};

export const RuntimeConfigContainer = connect(
  null,
  mapDispatchToProps
)(RuntimeConfig);
