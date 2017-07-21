// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Row, Col } from 'react-bootstrap';
import { sortBy } from 'lodash';
import { Map, TileLayer } from 'react-leaflet';
import MapMarker from './MapMarker';
import 'leaflet/dist/leaflet.css';
import { isValidObject, isNonEmptyArray } from 'utils/ObjectUtils';
import MarkerClusterLayer from './MarkerClusterLayer';
import UniverseRegionMarkerLayer from './UniverseRegionMarkerLayer';

import './stylesheets/RegionMap.scss'

export default class RegionMap extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['All', 'Universe', 'Provider']),
    showLabels: PropTypes.bool
  };

  static defaultProps = {
    showLabels: false
  }

  render() {
    const { regions, type, showLabels, universe } = this.props;
    var regionMarkers = []
    var bounds = [[61.96, 105.78], [-21.96, -95.78]];

    let regionData = type !== "Universe" ? regions : universe.regions;
      var regionLatLngs = regionData.map(function (region, idx) {
        var markerType = type;
        if (isValidObject(region.providerCode)) {
          markerType = region.providerCode;
        }
        regionMarkers.push(<MapMarker key={idx} latitude={region.latitude}
                                      longitude={region.longitude} type={markerType}/>)
        return [region.latitude, region.longitude];
      });

    if (type === "All") {
      regionMarkers =  <MarkerClusterLayer newMarkerData={regions}/>;
    }
    if (type === "Universe") {
      regionMarkers = <UniverseRegionMarkerLayer universe={universe}/>;
    }
    if (isNonEmptyArray(regionLatLngs) && type !== "All") {
      bounds = regionLatLngs;
    }
    const attribution =
      '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>' +
      ' contributors, &copy; <a href="https://carto.com/attributions">CARTO</a>';

    let regionMap =
      <Map bounds={bounds} center={[-1, 0]} zoom={1}
           zoomControl={false} className="yb-region-map" minZoom={1} maxZoom={5}
           touchZoom={false} scrollWheelZoom={false} doubleClickZoom={false} draggable={false}>
         <TileLayer
            attribution={attribution}
            url="http://{s}.basemaps.cartocdn.com/light_nolabels/{z}/{x}/{y}.png"/>
        {regionMarkers}
      </Map>;

    if (showLabels) {
      var regionLabels = <Col md={12}>No Regions Configured</Col>;
      if (isNonEmptyArray(regions)) {
        regionLabels = sortBy(regions, 'longitude').map((region) => {
          const zoneList = region.zones.map((zone) => {
            return <div key={`zone-${zone.uuid}`} className="zone-name">{zone.name}</div>;
          });
          return (
            <Col key={`region-${region.uuid}`} md={3} lg={2} className="region">
              <div className="region-name">{region.name}</div>
              {zoneList}
            </Col>
          );
        });
      }
      return (
        <span>
          <Row>
            <Col md={12}>
              {regionMap}
            </Col>
          </Row>
          <Row className="yb-map-labels">{regionLabels}</Row>
        </span>
      );
    }
    return regionMap;
  }
}
