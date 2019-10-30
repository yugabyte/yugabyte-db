// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import NavBarContainer from '../components/common/nav_bar/NavBarContainer';
import { Footer } from '../components/common/footer';
import AuthenticatedComponentContainer from '../components/Authenticated/AuthenticatedComponentContainer';
import { mouseTrap } from 'react-mousetrap';
import { browserHistory } from 'react-router';
import { YBModal } from '../components/common/forms/fields';
import { Table } from 'react-bootstrap';

import slackLogo from '../components/common/footer/images/slack-logo-full.svg';
import githubLogo from '../components/common/footer/images/github-light-small.png';

class AuthenticatedComponent extends Component {
  constructor(props) {
    super(props);
    props.bindShortcut(['ctrl+shift+n', 'ctrl+shift+m', 'ctrl+shift+t',
      'ctrl+shift+l', 'ctrl+shift+c', 'ctrl+shift+d'],
                            this._keyEvent);
    props.bindShortcut('?', this._toggleShortcutsHelp);
    this.state = {
      showKeyboardShortcuts: false,
      showIntroModal: false
    };
  }

  componentDidMount() {
    if (localStorage.getItem('__yb_new_user__') === 'true') {
      localStorage.setItem('__yb_new_user__', false);
      this.setState({ showIntroModal: true });
    }
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

  closeIntroModal = () => {
    this.setState({ showIntroModal: false });
  }

  render() {
    const { showKeyboardShortcuts, showIntroModal } = this.state;

    const socialMediaLinks = (
      <div className="footer-accessory-wrapper">
        <div>
          <a className="social-media-btn"
            href="https://www.yugabyte.com/slack"
            target="_blank"
            rel="noopener noreferrer"
          >
            <span>Join us on</span>
            <img alt="YugaByte DB Slack" src={slackLogo} width="65"/>
          </a>
        </div>
        <div>
          <a className="social-media-btn"
            href="https://github.com/yugabyte/yugabyte-db"
            target="_blank"
            rel="noopener noreferrer"
          >
            <span>Star us on</span>
            <img alt="YugaByte DB GitHub"
              className="social-media-logo"
              src={githubLogo} width="18"/> <b>GitHub</b>
          </a>
        </div>
      </div>
    );
    return (
      <AuthenticatedComponentContainer>
        <NavBarContainer />
        <div className="container-body">
          {this.props.children}
          <YBModal title={"Keyboard Shortcut"}
                   visible={showKeyboardShortcuts}
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
          <YBModal title="Welcome to YugaByte DB!"
                 visible={showIntroModal}
                 onHide={this.closeIntroModal}
                 showCancelButton={true}
                 cancelLabel={"Close"}
                 footerAccessory={socialMediaLinks}
          >
            <p>Thank you for downloading Yugabyte DB.</p>
            <p>Documentation can be found <a
              href="https://docs.yugabyte.com/latest/manage/enterprise-edition/"
              target="_blank" rel="noopener noreferrer">
                here.
              </a>
            </p>
          </YBModal>
        </div>
        <Footer />
      </AuthenticatedComponentContainer>
    );
  }
};

export default mouseTrap(AuthenticatedComponent);
