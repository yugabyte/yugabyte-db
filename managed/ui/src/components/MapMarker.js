// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Marker, Popup } from 'react-leaflet';
import { Icon }  from 'leaflet';
import CustomMarker from '../stylesheets/images/custom-marker.png';

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
        shadowUrl: CustomMarker,
        iconSize:     [25, 31],
        shadowSize:   [0, 0],
        popupAnchor:  [2, -12]
      });
    }
    return (
      <Marker position={[latitude, longitude]} {...opts}>
        {popup}
      </Marker>
    );
  }
}
