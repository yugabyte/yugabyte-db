var express = require('express');
var path = require('path');
var router = express.Router();
var app = express();

var staticPath = 'public';

app.use(express.static(staticPath));
app.use('/', express.static(staticPath));

app.use(function(req, res, next) {
  var err = new Error('Not Found');
  err.status = 404;
  next(err);
});

app.use(function(err, req, res, next) {
  res.status(err.status || 500);
  if(err.status === 500) {
    res.json({error: 'Internal Server Error'});
  }
  else if(err.status === 404) {
    res.render('error');    //render error page
  } else {
    res.json({error: err.message})
  }
});

module.exports = app;
