// Copyright (c) YugaByte, Inc.

$(document).ready(function() {
  var map = L.map( 'map', {
      center: [10.0, 5.0],
      minZoom: 2,
      zoom: 2,
      maxZoom: 2,
  });
  map.dragging.disable();

  L.tileLayer( 'http://{s}.mqcdn.com/tiles/1.0.0/map/{z}/{x}/{y}.png', {
      attribution: '&copy; <a href="http://osm.org/copyright" title="OpenStreetMap" target="_blank">OpenStreetMap</a> contributors | Tiles Courtesy of <a href="http://www.mapquest.com/" title="MapQuest" target="_blank">MapQuest</a> <img src="http://developer.mapquest.com/content/osm/mq_logo.png" width="16" height="16">',
      subdomains: ['otile1','otile2','otile3','otile4']
  }).addTo( map );

  $.get( "api/providers", function( results ) {
    $.each(results, function(idx, provider) {
      $.get("api/providers/" + provider.uuid + "/regions", function (regions) {
        $.each(regions, function(idx, region) {
          var marker = L.marker([region.latitude, region.longitude]).addTo(map);
          marker.bindPopup(region.name);
        });
      });
    });
  });
});
