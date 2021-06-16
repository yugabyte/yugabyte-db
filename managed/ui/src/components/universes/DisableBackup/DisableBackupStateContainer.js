import { connect } from "react-redux";
import { fetchUniverseInfo, fetchUniverseInfoResponse, updateBackupState, updateBackupStateResponse } from "../../../actions/universe";
import { ToggleBackupState } from "./DisableBackupState";


const mapDispatchToProps = (dispatch) => {
  return {
    updateBackupState: (universeUUID, flag) => {
      dispatch(updateBackupState(universeUUID, flag)).then((response) => {
        dispatch(updateBackupStateResponse(response.payload));
        dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
          dispatch(fetchUniverseInfoResponse(response.payload));
        });
      });
    }
  };
};

const mapStateToProps = (state) => {
  return {
    universe: state.universe.currentUniverse.data
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(ToggleBackupState);