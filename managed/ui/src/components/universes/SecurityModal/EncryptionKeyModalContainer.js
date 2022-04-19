// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { EncryptionKeyModal } from '../';
import { fetchAuthConfigList, fetchAuthConfigListResponse } from '../../../actions/cloud';
import {
  fetchUniverseInfo,
  fetchUniverseInfoResponse,
  setEncryptionKey,
  setEncryptionKeyResponse
} from '../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchKMSConfigList: () => {
      return dispatch(fetchAuthConfigList()).then((response) =>
        dispatch(fetchAuthConfigListResponse(response.payload))
      );
    },
    setEncryptionKey: (universeUUID, data) => {
      return dispatch(setEncryptionKey(universeUUID, data)).then((response) => {
        return dispatch(setEncryptionKeyResponse(response.payload));
      });
    },
    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    configList: state.cloud.authConfig,
    featureFlags: state.featureFlags
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(EncryptionKeyModal);
