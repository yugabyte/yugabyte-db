// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import OnPremInstances from './OnPremInstances';
import _ from 'lodash';
import {setOnPremConfigData} from '../../../../actions/cloud';
import {isDefinedNotNull, isNonEmptyObject, isNonEmptyArray} from 'utils/ObjectUtils';

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    setOnPremInstances: (formVals) => {
      var nodePayload = [];
      if (isNonEmptyObject(formVals.instances)) {
        Object.keys(formVals.instances).forEach(function (key) {
          formVals.instances[key].forEach(function (instanceTypeItem) {
            instanceTypeItem.instanceTypeIPs.split(",").forEach(function (ipItem, ipIdx) {
              nodePayload.push({
                ip: ipItem.trim(),
                zone: instanceTypeItem.zone,
                region: key,
                instanceType: instanceTypeItem.machineType,
                sshUser: formVals.sshUser
              });
            });
          });
        });
      }
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

const validate = values => {
  const errors = {};
  if (isNonEmptyObject(values.instances)) {
    errors.instances = {};
    Object.keys(values.instances).forEach(function(instanceKey){
      if (isNonEmptyArray(values.instances[instanceKey])) {
        errors.instances[instanceKey] = [];
        values.instances[instanceKey].forEach(function(node, nodeRowIdx){
          if (!isDefinedNotNull(node.zone)) {
            errors.instances[instanceKey][nodeRowIdx] = {'zone': 'required'};
          }
          if (!isDefinedNotNull(node.machineType)) {
            errors.instances[instanceKey][nodeRowIdx] = {'machineType': 'required'};
          }
          if (!isDefinedNotNull(node.instanceTypeIPs)) {
            errors.instances[instanceKey][nodeRowIdx] = {'instanceTypeIPs': 'required'};
          }
        })
      }
    })
  }
  return errors;
};

var onPremInstancesConfigForm = reduxForm({
  form: 'onPremConfigForm',
  destroyOnUnmount: false,
  validate,

});


export default connect(mapStateToProps, mapDispatchToProps)(onPremInstancesConfigForm(OnPremInstances));
