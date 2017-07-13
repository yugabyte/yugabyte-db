// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import {getPromiseState} from 'utils/PromiseUtils';
import {isNonEmptyObject, isNonEmptyArray} from 'utils/ObjectUtils';
import {YBButton, YBModal} from '../../common/forms/fields';
import {Row, Col} from 'react-bootstrap';
import InstanceTypeForRegion from '../OnPrem/wizard/InstanceTypeForRegion';
import {FieldArray} from 'redux-form';

export default class OnPremNodesList extends Component {
  constructor(props) {
    super(props);
    this.addNodeToList = this.addNodeToList.bind(this);
    this.hideModal = this.hideModal.bind(this);
    this.submitAddNodesForm = this.submitAddNodesForm.bind(this);
  }

  addNodeToList() {
    this.props.showAddNodesDialog();
  }

  hideModal() {
    this.props.reset();
    this.props.hideAddNodesDialog();
  }

  submitAddNodesForm(vals) {
    const {cloud: { supportedRegionList, accessKeys }} = this.props;
    var onPremProvider = this.props.cloud.providers.data.find((provider) => provider.code === "onprem");
    var self = this;
    let currentCloudRegions = supportedRegionList.data.filter(region => region.provider.code === "onprem");
    let currentCloudAccessKey = accessKeys.data.filter(
      accessKey => accessKey.idKey.providerUUID === onPremProvider.uuid
    ).shift()

    let zoneList = currentCloudRegions.reduce(function(azs, r) {
                         azs[r.code] = [];
                          r.zones.map((z) => azs[r.code][z.code.trim()] = z.uuid);
                         return azs
                       }, {});
    let instanceTypeList = [];
    if (isNonEmptyObject(vals.instances)) {
      instanceTypeList = Object.keys(vals.instances).map(function(region) {
        return vals.instances[region].reduce(function(acc, val) {
          let currentZone = val.zone.trim();
          let currentRegion = zoneList[region][currentZone];
          if (acc[currentRegion]) {
            val.instanceTypeIPs.split(",").forEach(function(ip){
              acc[zoneList[region][currentZone]].push({
                zone: val.zone, region: region, ip: ip.trim(),
                instanceType: val.machineType,
                sshUser: currentCloudAccessKey.keyInfo.sshUser
              })
            })
          } else {
            acc[currentRegion] = val.instanceTypeIPs.split(",").map((ip) =>
              ({zone: val.zone, region: region, ip: ip.trim(),
                instanceType: val.machineType,
                sshUser: currentCloudAccessKey.keyInfo.sshUser})
            );
          }
          return acc;
        }, {});
      });
      // Submit Node Payload
      self.props.createOnPremNodes(instanceTypeList, onPremProvider.uuid);
    }
    this.props.reset();
  }

  componentWillMount() {
    // Get OnPrem provider if provider list is already loaded during component load
    var onPremProvider = this.props.cloud.providers.data.find((provider)=>provider.code === "onprem");
    this.props.getRegionListItems(onPremProvider.uuid);
    this.props.getInstanceTypeListItems(onPremProvider.uuid);
  }

  render() {
    const {cloud :{nodeInstanceList, instanceTypes, supportedRegionList}, handleSubmit} = this.props;
    var nodeListItems = [];
    if (getPromiseState(nodeInstanceList).isSuccess()) {
      nodeListItems = nodeInstanceList.data.map(function(item) {
        return {
          nodeId: item.nodeUuID,
          inUse: item.inUse,
          ip: item.details.ip,
          instanceType: item.details.instanceType,
          region: item.details.region,
          zone: item.details.zone
        }
      });
    }
    var removeNodeItem = function(row, cell) {
      if (cell)
      return <i className={`fa fa-trash remove-cell-container`}/>;
    }

    let currentCloudRegions = supportedRegionList.data.filter(region => region.provider.code === "onprem");
    var regionFormTemplate = isNonEmptyArray(currentCloudRegions) ?
      currentCloudRegions.map(function(regionItem, idx){
        var zoneOptions = regionItem.zones.map(function(zoneItem, zoneIdx){
        return <option key={zoneItem+zoneIdx} value={zoneItem.code}>{zoneItem.code}</option>});
        var machineTypeOptions = instanceTypes.data.map(function(machineTypeItem, mcIdx){
        return <option key={machineTypeItem+mcIdx} value={machineTypeItem.instanceTypeCode}>{machineTypeItem.instanceTypeCode}</option>;
      });
        zoneOptions.unshift(<option key={-1} value={""}>Select</option>);
        machineTypeOptions.unshift(<option key={-1} value={""}>Select</option>);
        return (
          <div key={`instance${idx}`}>
            <div className="instance-region-type">{regionItem.code}</div>
            <div className="form-field-grid">
              <FieldArray name={`instances.${regionItem.code}`} component={InstanceTypeForRegion}
                          zoneOptions={zoneOptions} machineTypeOptions={machineTypeOptions} formType={"modal"}/>
            </div>
          </div>
        )
    }) : null;

    return (
      <div>
        <Row>
          <Col lg={2}><div className="node-list-heading" onClick={this.props.toggleNodesView}><i className="fa fa-chevron-circle-left"/>Back</div></Col>
          <Col lg={8}><div className="node-list-heading text-center">&nbsp;Nodes</div></Col>
          <Col lg={2}><YBButton btnText="Add Node" btnIcon="fa fa-plus" onClick={this.addNodeToList}/></Col>
        </Row>
        <Row>
          <BootstrapTable data={nodeListItems} >
            <TableHeaderColumn dataField="nodeId" isKey={true} hidden={true} />
            <TableHeaderColumn dataField="ip">IP</TableHeaderColumn>
            <TableHeaderColumn dataField="inUse">In Use</TableHeaderColumn>
            <TableHeaderColumn dataField="region">Region</TableHeaderColumn>
            <TableHeaderColumn dataField="zone">Zone</TableHeaderColumn>
            <TableHeaderColumn dataField="instanceType">Instance Type</TableHeaderColumn>
            <TableHeaderColumn dataField="" dataFormat={removeNodeItem}/>
          </BootstrapTable>
        </Row>
        <YBModal title={"Add Node"} formName={"AddNodeForm"} visible={this.props.visibleModal === "AddNodesForm"}
                 onHide={this.hideModal} onFormSubmit={handleSubmit(this.submitAddNodesForm)}>
          <div className="on-prem-form-text">
            Enter IP Addresses for the instances of each zone and machine type.
          </div>
          {regionFormTemplate}
        </YBModal>
      </div>
    )
  }
}
