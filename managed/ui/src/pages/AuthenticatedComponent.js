// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import NavBar from '../components/common/nav_bar/NavBar';
import { Footer } from '../components/common/footer';
import AuthenticatedComponentContainer from '../components/Authenticated/AuthenticatedComponentContainer';
import { mouseTrap } from 'react-mousetrap';
import { browserHistory } from 'react-router';
import { YBModal } from '../components/common/forms/fields';
import { Table } from 'react-bootstrap';

class AuthenticatedComponent extends Component {
  constructor(props) {
    super(props);
    this.state = { showKeyboardShortcuts: false };
  }

  componentWillMount() {
    this.props.bindShortcut(['ctrl+shift+n', 'ctrl+shift+m', 'ctrl+shift+t',
      'ctrl+shift+l', 'ctrl+shift+c', 'ctrl+shift+d'],
                            this._keyEvent);
    this.props.bindShortcut('?', this._toggleShortcutsHelp);
  }

  _toggleShortcutsHelp = () => {
    this.setState({showKeyboardShortcuts: !this.state.showKeyboardShortcuts});
  };

  _keyEvent = param => {
    switch(param.key) {
      case 'N':
        browserHistory.push("/universes/create");
        break;
      case 'C':
        browserHistory.push("/config");
        break;
      case 'M':
        browserHistory.push("/metrics");
        break;
      case 'T':
        browserHistory.push("/tasks");
        break;
      case 'L':
        browserHistory.push("/universes");
        break;
      case 'D':
        browserHistory.push("/");
        break;
      default:
        break;
    }
  };

  render() {
    return (
      <AuthenticatedComponentContainer>
        <NavBar />
        <div className="container-body">
          {this.props.children}
          <YBModal title={"Keyboard Shortcut"}
                   visible={this.state.showKeyboardShortcuts}
                   onHide={this._toggleShortcutsHelp}>
            <Table responsive>
              <thead>
                <tr><th>Shortcut</th><th>Description</th></tr>
              </thead>
              <tbody>
                <tr><td>?</td><td>Show help</td></tr>
                <tr><td>CTRL + SHIFT + n</td><td>Create new universe</td></tr>
                <tr><td>CTRL + SHIFT + e</td><td>Edit universe</td></tr>
                <tr><td>CTRL + SHIFT + c</td><td>Provider config</td></tr>
                <tr><td>CTRL + SHIFT + m</td><td>View metrics</td></tr>
                <tr><td>CTRL + SHIFT + t</td><td>View tasks</td></tr>
                <tr><td>CTRL + SHIFT +l</td><td>View universes</td></tr>
              </tbody>
            </Table>
          </YBModal>
        </div>
        <Footer />
      </AuthenticatedComponentContainer>
    );
  }
};

export default mouseTrap(AuthenticatedComponent);
