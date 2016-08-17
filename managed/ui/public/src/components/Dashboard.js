import React, { Component, PropTypes } from 'react';
import { Link } from 'react-router';
import DashboardRightPane from './DashboardRightPane';
export default class Dashboard extends Component {

    static contextTypes = {
        router: PropTypes.object
    };
    
    componentWillMount() {
        if(typeof this.props.customer =="undefined" || this.props.customer.status != "authenticated"){
            this.context.router.push('/login');
        }
    }
    
    render() {
        return (
        <div className="container-fluid">
            <nav className="navbar navbar-default ybNavBar">
                <div className="container-fluid">
                    <div className="navbar-header">
                        <a className="navbar-brand" href="#">
                            YugaByte Customer Portal
                        </a>
                    </div>
                </div>
            </nav>
            <div className="ybLeftPane">
                <ul className="ybLeftPaneItem">
                    <span className="ybLeftNavTop">
                      <li>Dashboard</li>
                    </span>
                </ul>
            </div>
            <div className="ybRightPane">
                <DashboardRightPane />
            </div>
        </div>
    );
  }
}
