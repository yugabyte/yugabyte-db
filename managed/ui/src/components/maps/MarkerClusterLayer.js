// Copyright (c) YugaByte, Inc.

import Leaflet from 'leaflet';
import { MapLayer } from 'react-leaflet';
require('leaflet.markercluster');
import 'leaflet.markercluster/dist/MarkerCluster.css';
import {isValidArray, isValidObject} from '../../utils/ObjectUtils';
import React, { Component } from 'react';
import './stylesheets/MarkerClusterLayer.css'
import ReactDOMServer from 'react-dom/server';
import {DescriptionList} from '../common/descriptors';

class MarkerItem extends Component {
  render() {
    const {markerDetail} = this.props;
    if (!isValidObject(markerDetail)) {
      return <span/>;
    }

    var markerListItems = [{"name": "provider","data": markerDetail.provider.name},
                           {"name": "region", "data": markerDetail.name}];
    if (isValidArray(markerDetail.universes)) {
      var universeDetailItem = markerDetail.universes.map(function(universeItem, universeIdx){
        return {"name": universeItem.name}
      })
      markerListItems.push({"name": "universes", "data": universeDetailItem})
    }

    return (
      <DescriptionList listItems={markerListItems}/>
    )
  }
}
export default class MarkerClusterLayer extends MapLayer {

  componentWillMount() {
    super.componentWillMount();
    this.leafletElement = Leaflet.markerClusterGroup({zoomToBoundsOnClick: false,
      spiderfyOnMaxZoom: false, singleMarkerMode: true,
      disableClusteringAtZoom: 3, maxClusterRadius: 20});
  }

  componentWillReceiveProps(nextProps) {
    var self = this;
    this.leafletElement.clearLayers();
    const {newMarkerData} = this.props;
    if (newMarkerData.length > 0) {
      let newMarkers = [];
      newMarkerData.forEach((obj) => {
        var popupDetail = ReactDOMServer.renderToString(<MarkerItem markerDetail={obj}/>);
        var latLng = Leaflet.latLng(obj.latitude, obj.longitude);
        let leafletMarker = new Leaflet.Marker(latLng,  {icon: new Leaflet.DivIcon({className: 'marker-cluster-small', html: 1}) })
          .bindPopup(popupDetail, {maxHeight: 100, maxWidth: 300, minWidth: 100});
        leafletMarker.ybData = obj;
        newMarkers.push(leafletMarker);
      });

      this.leafletElement.addLayers(newMarkers);
    }
    self.leafletElement.on('clusterclick', function(a) {
      var clusterMarker = "";
      a.layer.getAllChildMarkers().forEach(function(markerItem, markerIdx){
        clusterMarker += `${ReactDOMServer.renderToString(<MarkerItem markerDetail={markerItem._popup._source.ybData}/>)}`;
      });
      var marker = a.layer.getAllChildMarkers()[0];
      var cluster = a.target.getVisibleParent(marker);
      cluster.bindPopup(clusterMarker, { minWidth: 100}).openPopup();
    });
  }

  shouldComponentUpdate() {
    return false;
  }

  render() {
    return null;
  }
}
