//Copyright YugaByte Inc.

import CreateUniverse from '../components/CreateUniverse.js';
import {getRegionList, getRegionListSuccess, getRegionListFailure } from '../actions/cloud';
import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';

const getRegionListFromApi = (values, dispatch) => {
  return new Promise((resolve, reject) => {
    dispatch(getRegionList())
    .then((response) => {
      if(response.payload.status !== 200) {
        dispatch(getRegionListFailure(response.payload));
        reject(data); //this is for redux-form itself
      } else {
        dispatch(getRegionListSuccess(response.payload));
        resolve();
      }
    });
  });
};

const mapDispatchToProps = (dispatch) => {
  return {
    getRegionList: getRegionListFromApi,
    resetMe: () =>{

    }
  }
}


function mapStateToProps(state, ownProps) {
  return {
    customer: state.customer,
    universeName:null,
    universeProvider:null
  };
}


export default connect(mapStateToProps,mapDispatchToProps)(CreateUniverse);
