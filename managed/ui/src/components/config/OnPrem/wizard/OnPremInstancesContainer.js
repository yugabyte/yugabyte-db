// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import OnPremInstances from './OnPremInstances';
import _ from 'lodash';
import {setOnPremConfigData} from '../../../../actions/cloud';
const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    setOnPremInstances: (formVals) => {
      var nodePayload = [];
      Object.keys(formVals.instances).forEach(function(key){
        formVals.instances[key].forEach(function(instanceTypeItem){
          instanceTypeItem.instanceTypeIPs.split(",").forEach(function(ipItem, ipIdx){
            nodePayload.push({ip: ipItem, zone: instanceTypeItem.zone, region: key, instanceType: instanceTypeItem.machineType});
          });
        });
      });
      var onPremPayload = _.clone(ownProps.onPremJsonFormData);
      onPremPayload.nodes = nodePayload;
      dispatch(setOnPremConfigData(onPremPayload));
      ownProps.submitWizardJson(onPremPayload);
    }
  }
}

const mapStateToProps = (state) => {
  return {
    onPremJsonFormData: state.cloud.onPremJsonFormData
  };
}

var onPremInstancesConfigForm = reduxForm({
  form: 'onPremInstancesConfigForm',
  destroyOnUnmount: false
});


export default connect(mapStateToProps, mapDispatchToProps)(onPremInstancesConfigForm(OnPremInstances));
