import React, { Component } from 'react';
import { connect } from 'react-redux';
import Dashboard from '../components/Dashboard.js';


const mapStateToProps = (state) => {
  return { 
    customer: state.customer
  };
}

export default connect(mapStateToProps)(Dashboard);
