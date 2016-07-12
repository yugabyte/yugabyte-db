// Copyright (c) YugaByte, Inc.

$(document).ready(function() {
  var map = L.map( 'map', {
      center: [10.0, 5.0],
      minZoom: 2,
      zoom: 2,
      maxZoom: 2,
  });
  map.dragging.disable();

  // TODO: need to investigate if we can use some other tile
  L.tileLayer('http://korona.geog.uni-heidelberg.de/tiles/roads/x={x}&y={y}&z={z}', {
	attribution: 'Imagery from <a href="http://giscience.uni-hd.de/">GIScience Research Group @ University of Heidelberg</a> ' +
	             '&mdash; Map data &copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>'
  }).addTo (map);

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
