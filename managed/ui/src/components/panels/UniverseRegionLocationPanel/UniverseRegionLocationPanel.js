// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { RegionMap } from '../../maps';
import { RegionMapLegend } from '../../maps';
import { isNonEmptyArray, isValidObject, isEmptyArray } from 'utils/ObjectUtils';
import {getPromiseState} from 'utils/PromiseUtils';

export default class UniverseRegionLocationPanel extends Component {
  constructor(props) {
    super(props);
    this.onProviderSelect = this.onProviderSelect.bind(this);
    this.state = {selectedProviders: []};
  }

  onProviderSelect(selectedProviders) {
    this.setState({selectedProviders: selectedProviders.map(provider => provider.code)});
  }

  componentWillReceiveProps(nextProps) {
    if (isNonEmptyArray(nextProps.cloud.providers.data) && (nextProps.cloud.providers !== this.props.cloud.providers
        || isEmptyArray(this.state.selectedProviders))) {
      this.state = {selectedProviders: nextProps.cloud.providers.data.map(provider => provider.code)};
    }
  }

  render() {
    const { cloud, universe: {universeList}, cloud: {providers}} = this.props;
    var self = this;
    if (getPromiseState(providers).isEmpty() || !getPromiseState(cloud.supportedRegionList).isSuccess()) {
      return <span/>;
    }

    var completeRegionList = cloud.supportedRegionList.data.filter((region) => self.state.selectedProviders.includes(region.provider.code));
    var universeListByRegions = {};
    if (getPromiseState(universeList).isSuccess()) {
      universeList.data.forEach(function (universeItem) {
        if (isNonEmptyArray(universeItem.regions)) {
          universeItem.regions.forEach(function (regionItem) {
            if (isValidObject(regionItem.uuid)) {
              if (universeListByRegions.hasOwnProperty(regionItem.uuid)) {
                universeListByRegions[regionItem.uuid].push(universeItem);
              } else {
                universeListByRegions[regionItem.uuid] = [universeItem];
              }
            }
          });
        }
      });
    }
    completeRegionList.forEach(function(completeRegionItem, crIdx){
      delete completeRegionList[crIdx].universes;
      Object.keys(universeListByRegions).forEach(function(regionKey){
        if (regionKey === completeRegionItem.uuid) {
          completeRegionList[crIdx].universes = universeListByRegions[regionKey];
        }
      });
    });
    var completeProviderList = cloud.providers.data.map( (provider) => {
      return {code: provider.code, name: provider.name};
    });

    return (
      <div>
        <RegionMap title="All Supported Regions" regions={completeRegionList} type="All"/>
        { isNonEmptyArray(completeProviderList) && <RegionMapLegend providers={completeProviderList} onProviderSelect={this.onProviderSelect}/>}
      </div>
    );
  }
}
