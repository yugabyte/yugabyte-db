// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import {OnPremRegionsAndZones} from '../../../config';
import {setOnPremConfigData} from '../../../../actions/cloud';
import _ from 'lodash';

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    setOnPremRegionsAndZones: (formData) => {
      var payloadData = _.clone(ownProps.onPremJsonFormData);
      var regionsPayload = formData.regionsZonesList.map(function(regionItem){
        var regionLocation = regionItem.location.split(",");
        return {
          code: regionItem.code,
          zones: regionItem.zones.split(",").map(function(zoneItem){
            return zoneItem;
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

var onPremRegionsAndZonesForm = reduxForm({
  form: 'onPremRegionsAndZonesForm',
  destroyOnUnmount: false
});


export default connect(mapStateToProps, mapDispatchToProps)(onPremRegionsAndZonesForm(OnPremRegionsAndZones));
