// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Map, TileLayer } from 'react-leaflet';
import MapMarker from './MapMarker';
import 'leaflet/dist/leaflet.css';
import YBPanelItem from './YBPanelItem';
import Leaflet from 'leaflet'


export default class RegionMap extends Component {
  static propTypes = {
    regions: PropTypes.array.isRequired
  };

  constructor(props) {
    super(props);
    this.state = {
      zoom: 1,
      bounds: [
        [71.96, 175.78],
        [-71.96, -175.78]
      ]
    };
  }

  render() {
    const { regions } = this.props;

    const regionMarkers = []
    const regionLatLngs = regions.map(function(region, idx) {
      regionMarkers.push(<MapMarker key={idx} latitude={region.latitude} longitude={region.longitude} />)
      return [region.latitude, region.longitude];

    })
    var bounds = Leaflet.latLngBounds(regionLatLngs);

    const attribution =
      'Imagery from <a href="http://giscience.uni-hd.de/">GIScience Research Group @ University of Heidelberg</a> ' +
      '&mdash; Map data &copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>';

    return (
      <YBPanelItem name="Region Placement">
        <Map bounds={bounds} center={[-1, 0]} zoom={this.state.zoom} zoomControl={false} ref='map'>
          <TileLayer
            attribution={attribution}
            url='http://korona.geog.uni-heidelberg.de/tiles/roads/x={x}&y={y}&z={z}'/>
          {regionMarkers}
        </Map>
      </YBPanelItem>
    );
  }
}
