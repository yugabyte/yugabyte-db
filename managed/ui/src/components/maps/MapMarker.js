// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Marker, Popup } from 'react-leaflet';
import { Icon }  from 'leaflet';
import { DefaultMarkerIcon, DefaultMarkerShadowIcon, RootMarkerIcon } from './images'

export default class MapMarker extends Component {
  static propTypes = {
    latitude: PropTypes.number.isRequired,
    longitude: PropTypes.number.isRequired,
    label: PropTypes.string,
    type: PropTypes.string.isRequired
  };

  render() {
    const { latitude, longitude, label, type } = this.props;
    var popup;
    if (label) {
      popup = <Popup><span>{label}</span></Popup>
    }

    var opts = {};
    if( type === "Default" ) {
      opts['icon'] = new Icon({
        iconUrl: DefaultMarkerIcon,
        shadowUrl: DefaultMarkerShadowIcon,
        iconSize: [20, 27],
        popupAnchor: [10, 10],
        iconAnchor: [10, 30],
        shadowAnchor: [12, 46]
      });
    }
    else {
      var markerData = "";
      if (type === "Root") {
        markerData = RootMarkerIcon;
      }
      opts['icon'] = new Icon({
        iconUrl: markerData,
        shadowUrl: "",
        iconSize: [27, 27],
        popupAnchor: [10, 10],
        iconAnchor: [10, 30],
        shadowAnchor: [12, 46]
      });
    }
    return (
      <Marker position={[latitude, longitude]} {...opts}>
        {popup}
      </Marker>
    );
  }
}
