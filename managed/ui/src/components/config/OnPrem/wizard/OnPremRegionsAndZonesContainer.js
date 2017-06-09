// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import {OnPremRegionsAndZones} from '../../../config';
import {setOnPremConfigData} from '../../../../actions/cloud';
import _ from 'lodash';
import {isDefinedNotNull, isNonEmptyArray} from 'utils/ObjectUtils';

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    setOnPremRegionsAndZones: (formData) => {
      var payloadData = _.clone(ownProps.onPremJsonFormData);
      var regionsPayload = formData.regionsZonesList.map(function(regionItem){
        var regionLocation = regionItem.location.split(",");
        return {
          code: regionItem.code,
          zones: regionItem.zones.split(",").map(function(zoneItem){
            return zoneItem.trim();
          }),
          latitude: regionLocation[0],
          longitude: regionLocation[1]
        }
      });
      payloadData.regions = regionsPayload;
      dispatch(setOnPremConfigData(payloadData));
      ownProps.nextPage();
    }
  }
}

const mapStateToProps = (state) => {
  return {
    onPremJsonFormData: state.cloud.onPremJsonFormData
  };
}

const validate = values => {
  const errors = {regionsZonesList: []};
  if (values.regionsZonesList && isNonEmptyArray(values.regionsZonesList)) {
    values.regionsZonesList.forEach(function(regionZoneItem, rowIdx){
      if (!isDefinedNotNull(regionZoneItem.code)) {
        errors.regionsZonesList[rowIdx] = {'code': 'Required'};
      }
      if (!isDefinedNotNull(regionZoneItem.location)) {
        errors.regionsZonesList[rowIdx] = {'location': 'Required'};
      }
      if (!isDefinedNotNull(regionZoneItem.zones)) {
        errors.regionsZonesList[rowIdx] = {'zones': 'Required'};
      }
    });
  }
  return errors;
};

var onPremRegionsAndZonesForm = reduxForm({
  form: 'onPremConfigForm',
  validate,
  destroyOnUnmount: false
});


export default connect(mapStateToProps, mapDispatchToProps)(onPremRegionsAndZonesForm(OnPremRegionsAndZones));
