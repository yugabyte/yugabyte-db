// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBButton } from '../../../common/forms/fields';
import { FieldArray } from 'redux-form';
import InstanceTypeForRegion from './InstanceTypeForRegion';

export default class OnPremInstances extends Component {
  constructor(props) {
    super(props);
    this.submitInstances = this.submitInstances.bind(this);
  }
  submitInstances(formVals) {
    var submitPayload = [];
    Object.keys(formVals.instances).forEach(function(key){
      formVals.instances[key].forEach(function(instanceTypeItem){
        instanceTypeItem.instanceTypeIPs.split(",").forEach(function(ipItem, ipIdx){
          submitPayload.push({ip: ipItem, zone: instanceTypeItem.zone, region: key, instanceType: instanceTypeItem.machineType});
        });
      });
    });
  }

  render() {
    const {handleSubmit, switchToJsonEntry} = this.props;
    const {onPremJsonFormData} = this.props;
    if (!onPremJsonFormData) {
      return <span/>;
    }
    var regionFormTemplate = onPremJsonFormData.regions ? onPremJsonFormData.regions.map(function(regionItem, idx){
      var zoneOptions = regionItem.zones.map(function(zoneItem, zoneIdx){
        return <option key={zoneItem+zoneIdx} value={zoneItem}>{zoneItem}</option>});
      var machineTypeOptions = onPremJsonFormData.instanceTypes.map(function(machineTypeItem, mcIdx){
        return <option key={machineTypeItem+mcIdx} value={machineTypeItem.instanceTypeCode}>{machineTypeItem.instanceTypeCode}</option>;
      });
      zoneOptions.unshift(<option key={-1} value={""}>Select</option>);
      machineTypeOptions.unshift(<option key={-1} value={""}>Select</option>);
      return (
        <div key={`instance${idx}`}>
          <div className="instance-region-type">{regionItem.code}</div>
          <div className="form-field-grid">
            <FieldArray name={`instances.${regionItem.code}`} component={InstanceTypeForRegion}
                        zoneOptions={zoneOptions} machineTypeOptions={machineTypeOptions}/>
          </div>
        </div>
      )
    }) : null;
    return (
      <div className="on-prem-provider-form-container">
        <form name="onPremInstancesConfigForm" onSubmit={handleSubmit(this.props.setOnPremInstances)}>
          <div className="on-prem-form-text">
            Enter IP Addresses for the instances of each zone and machine type.
          </div>
          {regionFormTemplate}
          <div className="form-action-button-container">
            {switchToJsonEntry}
            <YBButton btnText={"Finish"} btnType={"submit"} btnClass={"btn btn-default save-btn"}/>
            <YBButton btnText={"Previous"}  btnClass={"btn btn-default back-btn"} onClick={this.props.prevPage}/>
          </div>
        </form>
      </div>
    )
  }
}
