import React from 'react';
import { Component } from 'react';
import Bootstrap from 'bootstrap';

export default class App extends Component {
	componentWillMount() {
    this.props.loadCustomerFromToken();
  }
  render() {
    return (
      <div>
        {this.props.children}
      </div>
    );
  }
}
