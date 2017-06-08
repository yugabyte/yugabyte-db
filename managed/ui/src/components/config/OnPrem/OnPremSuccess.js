// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBButton } from '../../common/forms/fields';
import { Row, Col } from 'react-bootstrap';
import {getPromiseState} from 'utils/PromiseUtils';

import {isNonEmptyArray, isDefinedNotNull} from 'utils/ObjectUtils';
import { YBConfirmModal } from '../../modals';
import { DescriptionList } from '../../common/descriptors';
import { RegionMap } from '../../maps';
const PROVIDER_TYPE = "onprem";
import OnPremNodesListContainer from './OnPremNodesListContainer';

export default class OnPremSuccess extends Component {
  constructor(props) {
    super(props);
    this.toggleNodesView = this.toggleNodesView.bind(this);
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
    }
  }
  toggleNodesView() {
    this.setState({nodesPageActive: !this.state.nodesPageActive});
  }
  componentWillReceiveProps(nextProps) {
    if (nextProps.cloudBootstrap !== this.props.cloudBootstrap) {
      if (nextProps.cloudBootstrap.data.type === "cleanup" && isDefinedNotNull(nextProps.cloudBootstrap.data.response)) {
        this.props.resetConfigForm();
        this.props.fetchCloudMetadata();
      }
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
                          btnClass={"btn btn-default yb-button"} onClick={this.props.showDeleteProviderModal}/>
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
