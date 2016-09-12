// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import UniverseCostBreakDownPanel from '../../components/panels/UniverseCostBreakDownPanel';
import { fetchUniverseList, fetchUniverseListSuccess,
         fetchUniverseListFailure, fetchCustomerCost,
         fetchCustomerCostSuccess, fetchCustomerCostFailue, resetCustomerCost }
         from '../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseCost: () => {
      dispatch(fetchUniverseList())
        .then((response) => {
          if (response.payload.status !== 200) {
            dispatch(fetchUniverseListFailure(response.payload));
          } else {
            dispatch(fetchUniverseListSuccess(response.payload));
            response.payload.data.map(function(item,idx){
              return (
                dispatch(fetchCustomerCost(item.universeUUID))
                .then((response) => {
                  if(response.payload.status !== 200) {
                    dispatch(fetchCustomerCostFailue(response.payload.data));
                  } else {
                    dispatch(fetchCustomerCostSuccess(response.payload.data));
                  }
                })
              );
            });
          }
        });
    },
    resetCustomerCost: () => {
      dispatch(resetCustomerCost());
    }
  }
}

function mapStateToProps(state) {
  return {
    universe: state.universe
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseCostBreakDownPanel);
