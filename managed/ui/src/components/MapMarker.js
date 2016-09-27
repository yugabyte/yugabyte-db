// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Marker, Popup } from 'react-leaflet';
import { Icon }  from 'leaflet';
import CustomMarker from '../stylesheets/images/marker.png';
import ShadowMarker from '../stylesheets/images/marker-shadow.png';
export default class MapMarker extends Component {
  static propTypes = {
    latitude: PropTypes.number.isRequired,
    longitude: PropTypes.number.isRequired,
    label: PropTypes.string,
    customIcon: PropTypes.bool
  };

  static defaultProps = {
    customIcon: false
  }

  render() {
    const { latitude, longitude, label, customIcon } = this.props;
    var popup;
    if (label) {
      popup = <Popup><span>{label}</span></Popup>
    }

    var opts = {};
    if (customIcon) {
      opts['icon'] = new Icon({
        iconUrl: CustomMarker,
        shadowUrl: ShadowMarker,
        iconSize:     [20, 27],
        popupAnchor:  [10, 10],
        iconAnchor:   [10,30],
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
