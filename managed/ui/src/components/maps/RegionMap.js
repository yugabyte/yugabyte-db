// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Map, TileLayer } from 'react-leaflet';
import MapMarker from './MapMarker';
import 'leaflet/dist/leaflet.css';
import {isValidObject, isValidArray} from '../../utils/ObjectUtils';
import MarkerClusterLayer from './MarkerClusterLayer';

import './stylesheets/RegionMap.css'

export default class RegionMap extends Component {
  static propTypes = {
    regions: PropTypes.array.isRequired,
    type: PropTypes.string.isRequired
  };

  render() {
    const { regions, type } = this.props;
    var regionMarkers = []
    var bounds = [[61.96, 105.78], [-21.96, -95.78]];
    var regionLatLngs = regions.map(function (region, idx) {
      var markerType = type;
      if (isValidObject(region.providerCode)) {
        markerType = region.providerCode;
      }
      regionMarkers.push(<MapMarker key={idx} latitude={region.latitude}
                                    longitude={region.longitude} type={markerType}/>)
      return [region.latitude, region.longitude];
    });
    if (type === "all") {
      regionMarkers =  <MarkerClusterLayer newMarkerData={regions}/>
    }
    if (isValidArray(regionLatLngs) && regionLatLngs.length >= 1) {
      bounds = regionLatLngs;
    }
    const attribution =
      '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>' +
      ' contributors, &copy; <a href="https://carto.com/attributions">CARTO</a>';

    return (
      <Map bounds={bounds} center={[-1, 0]} zoom={1}
           zoomControl={false} className="region-map-container" minZoom={1} maxZoom={5}
           touchZoom={false} scrollWheelZoom={false} doubleClickZoom={false} draggable={false}>
         <TileLayer
            attribution={attribution}
            url="http://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}@2x.png"/>
        {regionMarkers}
      </Map>
    );
  }
}
