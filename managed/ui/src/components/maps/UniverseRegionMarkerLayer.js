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
    const markerData =  universe.regions;
    const currentRegion = markerData.find(function(markerItem){
      return markerItem.uuid === regionUUID ;
    });
    return currentRegion;
  }
  render() {
    const self = this;
    const { universe } = this.props;
    const cloudList = universe.universeDetails.placementInfo.cloudList;
    const azMarkerPoints = [];
    const markerDataArray = [];
    cloudList.forEach(function(cloudItem){
      cloudItem.regionList.forEach(function(regionItem, regionIdx){
        const regionMarkerIcon = divIcon({className: 'universe-region-marker'});
        const currentRegion = self.getCurrentRegion(regionItem.uuid);
        const regionLatLong = [currentRegion.latitude, currentRegion.longitude];
        const azPoints = getPointsOnCircle(regionItem.azList.length, regionLatLong, 1);
        azPoints.forEach(function(azPoint){
          azMarkerPoints.push(azPoint);
        });
        azPoints.forEach(function(azItem, azIdx){
          const label = (
            <span>
              <div>Region: {regionItem.name}</div>
              <div>Availability Zone: {regionItem.azList[azIdx].name}</div>
              <div>Nodes: {regionItem.azList[azIdx].numNodesInAZ}</div>
            </span>
          );
          markerDataArray.push(
            <MapMarker key={"az-marker-" + regionIdx + azIdx} type="AZMarker" latitude={azItem[0]} longitude={azItem[1]} label={label} labelType={"tooltip"}/>
          );
        });
        markerDataArray.push(<Marker key={regionIdx+"-region-marker"} position={[currentRegion.latitude, currentRegion.longitude]} icon={regionMarkerIcon}/>);
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
