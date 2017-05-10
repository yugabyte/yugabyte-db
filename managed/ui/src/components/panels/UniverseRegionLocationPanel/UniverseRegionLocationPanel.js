// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { RegionMap } from '../../maps';
import { RegionMapLegend } from '../../maps';
import {isValidArray, isValidObject} from '../../../utils/ObjectUtils';

export default class UniverseRegionLocationPanel extends Component {

  render() {
    const { cloud, universe: {universeList} } = this.props;

    var completeRegionList = cloud.supportedRegionList.data;
    var universeListByRegions = {};
    universeList.forEach(function(universeItem){
      if (isValidArray(universeItem.regions)) {
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
    completeRegionList.forEach(function(completeRegionItem, crIdx){
      delete completeRegionList[crIdx].universes;
      Object.keys(universeListByRegions).forEach(function(regionKey){
        if (regionKey === completeRegionItem.uuid) {
          completeRegionList[crIdx].universes = universeListByRegions[regionKey];
        }
      });
    });
    return (
      <div>
        <RegionMap title="All Supported Regions" regions={completeRegionList} type="all"/>
        <RegionMapLegend providers={cloud.providers}/>
      </div>
    )
  }
}
