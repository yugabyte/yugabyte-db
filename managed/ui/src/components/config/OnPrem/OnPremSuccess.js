// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBButton } from '../../common/forms/fields';
import { Row, Col } from 'react-bootstrap';
import { getPromiseState } from 'utils/PromiseUtils';

import { isNonEmptyArray, isDefinedNotNull, isEmptyObject } from 'utils/ObjectUtils';
import { YBConfirmModal } from '../../modals';
import { DescriptionList } from '../../common/descriptors';
import { RegionMap } from '../../maps';
import OnPremNodesListContainer from './OnPremNodesListContainer';

const PROVIDER_TYPE = "onprem";

export default class OnPremSuccess extends Component {
  constructor(props) {
    super(props);
    this.toggleNodesView = this.toggleNodesView.bind(this);
    this.getReadyState = this.getReadyState.bind(this);
    this.state = {nodesPageActive: false};
  }

  deleteProvider(uuid) {
    this.props.deleteProviderConfig(uuid);
  }

  componentWillMount() {
    const {configuredProviders} = this.props;
    let currentProvider = configuredProviders.data.find(provider => provider.code === 'onprem');
    if (isDefinedNotNull(currentProvider)) {
      this.props.fetchAccessKeysList(currentProvider.uuid);
      this.props.fetchConfiguredNodeList(currentProvider.uuid);
      this.props.fetchInstanceTypeList(currentProvider.uuid);
    }
  }

  toggleNodesView() {
    this.setState({nodesPageActive: !this.state.nodesPageActive});
  }

  getReadyState(dataObject) {
    return getPromiseState(dataObject).isSuccess() || getPromiseState(dataObject).isEmpty();
  }

  componentWillReceiveProps(nextProps) {
    if (nextProps.cloudBootstrap !== this.props.cloudBootstrap) {
      if (nextProps.cloudBootstrap.data.type === "cleanup" && isDefinedNotNull(nextProps.cloudBootstrap.data.response)) {
        this.props.resetConfigForm();
        this.props.fetchCloudMetadata();
      }
    }

    const {configuredRegions, configuredProviders, accessKeys, cloud: {nodeInstanceList, instanceTypes, onPremJsonFormData}} = nextProps;
    // Set OnPremJSONFormData if not set
    if (isEmptyObject(onPremJsonFormData) && this.getReadyState(nodeInstanceList) && this.getReadyState(instanceTypes) && this.getReadyState(accessKeys)) {
      const onPremRegions = configuredRegions.data.filter(
        (configuredRegion) => configuredRegion.provider.code === PROVIDER_TYPE
      );
      let currentProvider = configuredProviders.data.find(provider => provider.code === 'onprem');
      let onPremAccessKey = {};
      let jsonDataBlob = {};
      if (isNonEmptyArray(accessKeys.data)) {
        onPremAccessKey = accessKeys.data.find((accessKey) => accessKey.idKey.providerUUID === currentProvider.uuid)
      }
      jsonDataBlob.provider = {name: currentProvider.name};
      jsonDataBlob.key = {
        code: onPremAccessKey.idKey.keyCode,
        privateKeyContent: onPremAccessKey.keyInfo.privateKey
      };
      jsonDataBlob.regions = onPremRegions.map(function(regionItem){
        return {code: regionItem.code,
                longitude: regionItem.longitude,
                latitude: regionItem.latitude,
                zones: regionItem.zones
                  .map(function (zoneItem) {
                    return zoneItem.code
                  })
        }
      });
      jsonDataBlob.instanceTypes = instanceTypes.data.map(function(instanceTypeItem){
        return {instanceTypeCode: instanceTypeItem.instanceTypeCode,
                numCores: instanceTypeItem.numCores,
                memSizeGB: instanceTypeItem.memSizeGB,
                volumeDetailsList: instanceTypeItem.instanceTypeDetails.volumeDetailsList
                  .map(function (volumeDetailItem) {
                    return {volumeSizeGB: volumeDetailItem.volumeSizeGB,
                            volumeType: volumeDetailItem.volumeType,
                            mountPath: volumeDetailItem.mountPath}})}
      });
      jsonDataBlob.nodes = nodeInstanceList.data.map(function(nodeItem){
        return {
          ip: nodeItem.details.ip,
          region: nodeItem.details.region,
          zone: nodeItem.details.zone,
          instanceType: nodeItem.details.instanceType
        }
      });
      this.props.setOnPremJsonData(jsonDataBlob);
    }
  }
  render() {
    const {configuredRegions, configuredProviders, accessKeys, universeList, cloud: {nodeInstanceList}} = this.props;
    const onPremRegions = configuredRegions.data.filter(
      (configuredRegion) => configuredRegion.provider.code === PROVIDER_TYPE
    );
    let currentProvider = configuredProviders.data.find(provider => provider.code === 'onprem');
    if (!currentProvider) {
      return <span/>;
    }
    let accessKeyList = "Not Configured";
    if (isNonEmptyArray(accessKeys.data)) {
      accessKeyList = accessKeys.data.map((accessKey) => accessKey.idKey.keyCode).join(", ")
    }
    let universeExistsForProvider = universeList.data.some(universe => universe.provider && (universe.provider.uuid === currentProvider.uuid));
    var nodeListString = getPromiseState(nodeInstanceList).isEmpty() ? "No Nodes Configured" : nodeInstanceList.data.map(function (nodeItem) {
      return nodeItem.details.ip
    }).join(", ");
    var nodeItemObject = <div>{nodeListString} </div>;

    const providerInfo = [
      {name: "Account Name", data: currentProvider.name},
      {name: "Key Pair", data: accessKeyList},
      {name: <span className="node-link-container" onClick={this.toggleNodesView}> Nodes</span>, data: nodeItemObject},
    ];
    if (this.state.nodesPageActive) {
      return <OnPremNodesListContainer toggleNodesView={this.toggleNodesView}/>
    } else {
      return (
        <div>
          <Row className="config-section-header">
            <Col md={12}>
             <span className="pull-right" title={"Delete Provider"}>
                <YBButton btnText="Edit Configuration" disabled={false} btnIcon="fa fa-pencil"
                          btnClass={"btn btn-default yb-button"} onClick={this.props.showEditProviderForm}/>
                <YBButton btnText="Delete Configuration" disabled={universeExistsForProvider} btnIcon="fa fa-trash"
                          btnClass={"btn btn-default yb-button"} onClick={this.props.showDeleteProviderModal}/>
                <YBConfirmModal name="delete-aws-provider" title={"Confirm Delete"}
                                onConfirm={this.deleteProvider.bind(this, currentProvider.uuid)}
                                currentModal="deleteOnPremProvider" visibleModal={this.props.visibleModal}
                                hideConfirmModal={this.props.hideDeleteProviderModal}>
                  Are you sure you want to delete this OnPrem configuration?
                </YBConfirmModal>
             </span>
              <DescriptionList listItems={providerInfo}/>
            </Col>
          </Row>
          <RegionMap title="All Supported Regions" regions={onPremRegions} type="Root" showLabels={true}/>
        </div>
      )
    }
  }
}
