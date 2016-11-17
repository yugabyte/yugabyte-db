// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Map, TileLayer } from 'react-leaflet';
import MapMarker from './MapMarker';
import 'leaflet/dist/leaflet.css';
import {isValidArray} from '../utils/ObjectUtils';

export default class RegionMap extends Component {
  static propTypes = {
    regions: PropTypes.array.isRequired
  };

  render() {
    const { regions, type } = this.props;
    const regionMarkers = []
    var bounds = [[71.96, 175.78],
      [-71.96, -175.78]]
    const regionLatLngs = regions.map(function(region, idx) {
      regionMarkers.push(<MapMarker key={idx} latitude={region.latitude} longitude={region.longitude} type={type}/>)
      return [region.latitude, region.longitude];
    })

    if (isValidArray(regionLatLngs) && regionLatLngs.length >=2) {
      bounds = regionLatLngs;
    }

    const attribution =
      '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>' +
      ' contributors, &copy; <a href="https://carto.com/attributions">CARTO</a>';

    return (
        <Map bounds={bounds} center={[-1, 0]} zoom={1}
         zoomControl={false} ref='map' className="region-map-container" minZoom={1} maxZoom={5}
         touchZoom={false} scrollWheelZoom={false} doubleClickZoom={false} draggable={false}>
           <TileLayer
             attribution={attribution}
             url="http://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}@2x.png"/>
           {regionMarkers}
        </Map>
    );
  }
}
