// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import {OnPremMachineTypes} from '../../../config';
import {setOnPremConfigData} from '../../../../actions/cloud';
import _ from 'lodash';

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    submitOnPremMachineTypes: (formData) => {
      var payloadObject = _.clone(ownProps.onPremJsonFormData);
      var instanceTypesList = formData.machineTypeList.map(function(item, idx){
        return {instanceTypeCode: item.code,
          numCores: item.numCores, memSizeGB: item.memSizeGB,
          volumeDetailsList: item.mountPath.split(",").map(function(mountPathItem, mpIdx){
            return {volumeSizeGB: item.volumeSizeGB, volumeType: item.volumeType, mountPath: mountPathItem}
          })}
      });
      payloadObject.instanceTypes = instanceTypesList;
      dispatch(setOnPremConfigData(payloadObject));
      ownProps.nextPage();
    }
  }
}

const mapStateToProps = (state) => {
  return {
    onPremJsonFormData: state.cloud.onPremJsonFormData
  };
}

var onPremMachineTypesConfigForm = reduxForm({
  form: 'onPremMachineTypesConfigForm',
  destroyOnUnmount: false
});


export default connect(mapStateToProps, mapDispatchToProps)(onPremMachineTypesConfigForm(OnPremMachineTypes));
