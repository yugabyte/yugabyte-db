// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Row, Col } from 'react-bootstrap';
import { sortBy } from 'lodash';
import { Map, TileLayer } from 'react-leaflet';
import MapMarker from './MapMarker';
import 'leaflet/dist/leaflet.css';
import MarkerClusterLayer from './MarkerClusterLayer';
import UniverseRegionMarkerLayer from './UniverseRegionMarkerLayer';
import {MAP_SERVER_URL} from '../../config';
import './stylesheets/RegionMap.scss';
import { getPrimaryCluster } from '../../utils/UniverseUtils';
import { isNonEmptyArray, isDefinedNotNull } from '../../utils/ObjectUtils';

export default class RegionMap extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['All', 'Universe', 'Region', 'Table']),
    showLabels: PropTypes.bool
  };

  static defaultProps = {
    showLabels: false,
    showRegionLegend: true,
  };

  constructor(props) {
    super(props);
    this.state = {loadMarkers: false};
  }

  onMapZoomEnd = () => {
    this.setState({loadMarkers: true});
  };

  render() {
    const { regions, type, showLabels, showRegionLabels, universe } = this.props;
    let regionMarkers = [];
    let bounds = [[61.96, 105.78], [-21.96, -95.78]];
    let regionData = regions;
    if (type === "Universe") {
      const primaryCluster = getPrimaryCluster(universe.universeDetails.clusters);
      if (isDefinedNotNull(primaryCluster)) regionData = primaryCluster.regions;
    }
    const regionLatLngs = regionData.map(function (region, idx) {
      let markerType = type;
      if (isDefinedNotNull(region.providerCode)) {
        markerType = region.providerCode;
      }
      const numChildren = region.zones.length;
      if (type === "Region") {
        regionMarkers.push(
          <MapMarker key={idx} latitude={region.latitude}
                     longitude={region.longitude} type={markerType}
                     numChildren={numChildren}/>
        );
      }
      return [region.latitude, region.longitude];
    });
    if (type === "All") {
      regionMarkers = <MarkerClusterLayer newMarkerData={regions}/>;
    }
    if (type === "Universe" && this.state.loadMarkers) {
      regionMarkers = <UniverseRegionMarkerLayer universe={universe}/>;
    }
    if (type === "Table" && this.state.loadMarkers) {
      regionMarkers = regionData.map(function (region, idx) {
        return (<MapMarker key={idx} latitude={region.latitude}
                                    longitude={region.longitude} type={type}/>);
      });
    }
    if (isNonEmptyArray(regionLatLngs) && type !== "All") {
      bounds = regionLatLngs;
    }
    const attribution =
      'Copyright &copy; MapBox All rights reserved';

    const regionMap = (
      <Map bounds={bounds} center={[-1, 0]} zoom={1}
           zoomControl={false} className="yb-region-map" minZoom={1} maxZoom={5}
           touchZoom={false} scrollWheelZoom={false} doubleClickZoom={false}
           draggable={false} onzoomend={this.onMapZoomEnd}>
        <TileLayer
            attribution={attribution}
            url={`${MAP_SERVER_URL}/{z}/{x}/{y}.png`}/>
        {regionMarkers}
      </Map>
    );

    if (showLabels) {
      let regionLabels = <Col md={12}>No Regions Configured</Col>;
      if (isNonEmptyArray(regions)) {
        regionLabels = sortBy(regions, 'longitude').map((region) => {
          const zoneList = sortBy(region.zones, 'name').map((zone) => {
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
          { (showRegionLabels && <Row className="yb-map-labels">{regionLabels}</Row>) || null }
          <Row>
            <Col md={12}>
              {regionMap}
            </Col>
          </Row>
        </span>
      );
    }
    return regionMap;
  }
}
