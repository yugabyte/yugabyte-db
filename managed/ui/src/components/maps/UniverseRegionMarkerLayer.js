// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Marker, FeatureGroup, Polygon } from 'react-leaflet';
import { divIcon } from 'leaflet';
import MapMarker from './MapMarker';
import {getPointsOnCircle} from 'utils/ObjectUtils';
import './stylesheets/universeRegionMarkerLayer.scss';

export default class UniverseRegionMarkerLayer extends Component {
  constructor(props) {
    super(props);
    this.getCurrentRegion = this.getCurrentRegion.bind(this);
  }
  getCurrentRegion(regionUUID) {
    const {universe} = this.props;
    let markerData =  universe.regions;
    let currentRegion = markerData.find(function(markerItem){
      return markerItem.uuid === regionUUID ;
    })
    return currentRegion;
  }
  render() {
    let self = this;
    const { universe } = this.props;
    let cloudList = universe.universeDetails.placementInfo.cloudList;
    let azMarkerPoints = [];
    let markerDataArray = [];
    cloudList.forEach(function(cloudItem){
      cloudItem.regionList.forEach(function(regionItem, regionIdx){
        let regionMarkerIcon = divIcon({className: 'universe-region-marker', html: regionItem.name});
        let currentRegion = self.getCurrentRegion(regionItem.uuid);
        let regionLatLong = [currentRegion.latitude, currentRegion.longitude];
        let azPoints = getPointsOnCircle(regionItem.azList.length, regionLatLong, 1);
        azPoints.forEach(function(azPoint){
          azMarkerPoints.push(azPoint);
        });
        azPoints.forEach(function(azItem, azIdx){
          let label = (
            <span>
              <div>Name: {regionItem.azList[azIdx].name}</div>
              <div>Number Of Nodes: {regionItem.azList[azIdx].numNodesInAZ}</div>
            </span>
          );
          markerDataArray.push(
            <MapMarker key={"az-marker-" + regionIdx + azIdx} type="AZMarker" latitude={azItem[0]} longitude={azItem[1]} label={label} labelType={"tooltip"}/>
          )
        });
        markerDataArray.push(<Marker key={regionIdx+"-region-marker"} position={[currentRegion.latitude, currentRegion.longitude]} icon={regionMarkerIcon}/>)
      });
    });
    markerDataArray.push(<Polygon key={"az-line-polygon"} color="#A9A9A9" fillColor="transparent" positions={azMarkerPoints} />);
    return (
      <div>
        <FeatureGroup>{markerDataArray}</FeatureGroup>
      </div>
    );
  }
}
