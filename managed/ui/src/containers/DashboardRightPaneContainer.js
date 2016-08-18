// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import DashboardRightPane from '../components/DashboardRightPane.js';


const mapStateToProps = (state) => {
  return {
    regions: state.regions
  };
}

const mapDispatchToProps = (dispatch) => {
  return {
    showCreateUniverseForm: createUniverse,
    resetMe: () =>{

    }
  }
}

const createUniverse = (values, dispatch) => {

};

export default connect(mapStateToProps)(DashboardRightPane);
