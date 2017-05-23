// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Row, Col } from 'react-bootstrap';
import { sortBy } from 'lodash';
import { Map, TileLayer } from 'react-leaflet';
import MapMarker from './MapMarker';
import 'leaflet/dist/leaflet.css';
import { isValidObject, isNonEmptyArray } from 'utils/ObjectUtils';
import MarkerClusterLayer from './MarkerClusterLayer';

import './stylesheets/RegionMap.scss'

export default class RegionMap extends Component {
  static propTypes = {
    regions: PropTypes.array.isRequired,
    type: PropTypes.string.isRequired,
    showLabels: PropTypes.bool
  };

  static defaultProps = {
    showLabels: false
  }

  render() {
    const { regions, type, showLabels } = this.props;
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
      regionMarkers =  <MarkerClusterLayer newMarkerData={regions}/>;
    }

    if (isNonEmptyArray(regionLatLngs) && type !== "all") {
      bounds = regionLatLngs;
    }
    const attribution =
      '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>' +
      ' contributors, &copy; <a href="https://carto.com/attributions">CARTO</a>';
    let regionMap =
      <Map bounds={bounds} center={[-1, 0]} zoom={1}
           zoomControl={false} className="region-map-container" minZoom={1} maxZoom={5}
           touchZoom={false} scrollWheelZoom={false} doubleClickZoom={false} draggable={false}>
         <TileLayer
            attribution={attribution}
            url="http://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}@2x.png"/>
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
          <Row className="map-labels">{regionLabels}</Row>
        </span>
      );
    }
    return regionMap;
  }
}
