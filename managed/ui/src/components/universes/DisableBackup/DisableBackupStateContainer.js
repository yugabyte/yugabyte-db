import { connect } from "react-redux";
import { toast } from "react-toastify";
import { fetchUniverseInfo, fetchUniverseInfoResponse, updateBackupState, updateBackupStateResponse } from "../../../actions/universe";
import { ToggleBackupState } from "./DisableBackupState";


const mapDispatchToProps = (dispatch) => {
  return {
    updateBackupState: (universeUUID, flag) => {
      dispatch(updateBackupState(universeUUID, flag)).then((response) => {
        if (response.error) {
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(errorMessage);
        } else {
          toast.success('Successfully Disabled the backups.');
        }
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
