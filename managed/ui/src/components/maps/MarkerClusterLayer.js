// Copyright (c) YugaByte, Inc.

import Leaflet from 'leaflet';
import { MapLayer } from 'react-leaflet';
require('leaflet.markercluster');
import 'leaflet.markercluster/dist/MarkerCluster.css';

import './stylesheets/MarkerClusterLayer.css'

export default class MarkerClusterLayer extends MapLayer {
  componentWillMount() {
    super.componentWillMount();
    this.leafletElement = Leaflet.markerClusterGroup({zoomToBoundsOnClick: false,
      disableClusteringAtZoom: 3, maxClusterRadius: 20});
  }
  componentDidMount() {
    super.componentDidMount();
    const {newMarkerData} = this.props;
    if (newMarkerData.length > 0) {
      let newMarkers = [];
      newMarkerData.forEach((obj) => {
        var latLng = Leaflet.latLng(obj.latitude, obj.longitude);
        let leafletMarker = new Leaflet.Marker(latLng,  {icon: new Leaflet.DivIcon({className: 'marker-cluster-small', html: 1}) })
        newMarkers.push(leafletMarker);
      });
      this.leafletElement.addLayers(newMarkers);
    }
  }
  shouldComponentUpdate() {
    return false;
  }
  render() {
    return null;
  }
}
