// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { TableDetail } from '../../tables';
import {fetchTableDetail, fetchTableDetailFailure, fetchTableDetailSuccess, resetTableDetail} from '../../../actions/tables';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchTableDetail: (universeUUID, tableUUID) => {
      dispatch(fetchTableDetail(universeUUID, tableUUID)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(fetchTableDetailFailure(response.payload));
        } else {
          dispatch(fetchTableDetailSuccess(response.payload));
        }
      });
    },
    resetTableDetail:() => {
      dispatch(resetTableDetail());
    }
  }
};

function mapStateToProps(state) {
  return {
    universe: state.universe,
    tables: state.tables
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(TableDetail);
