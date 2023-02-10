// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { UniverseDisplayPanel } from '../../../components/panels';
import {
  fetchRunTimeConfigs,
  fetchRunTimeConfigsResponse,
  DEFAULT_RUNTIME_GLOBAL_SCOPE
} from '../../../actions/customers';
import { closeUniverseDialog, fetchUniverseMetadata } from '../../../actions/universe';
import { openDialog, closeDialog } from '../../../actions/modal';

const mapDispatchToProps = (dispatch) => {
  return {
    showUniverseModal: () => {
      dispatch(openDialog('universeModal'));
    },
    closeUniverseModal: () => {
      dispatch(closeDialog());
      dispatch(closeUniverseDialog());
    },
    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    },
    fetchGlobalRunTimeConfigs: () => {
      return dispatch(fetchRunTimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE, true)).then((response) =>
        dispatch(fetchRunTimeConfigsResponse(response.payload))
      );
    }
  };
};

const mapStateToProps = (state) => {
  return {
    runtimeConfigs: state.customer.runtimeConfigs
  };
};
export default connect(mapStateToProps, mapDispatchToProps)(UniverseDisplayPanel);
