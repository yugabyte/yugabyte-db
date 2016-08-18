// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import Modal from 'react-modal';

export default class DashboardRightPane extends Component {
  
  componentWillMount() {

  }

  constructor(props) {
    super(props);
    this.showCreateInstanceModal = this.showCreateInstanceModal.bind(this);
    this.closeModal=this.closeModal.bind(this);
    this.state = {
      isModalOpen: false
    };
  }

  showCreateInstanceModal(){
    this.setState({isModalOpen:true});
  }
  closeModal(){
    this.setState({isModalOpen: false});
  }

  render() {

    return (
      <div>
      <table className="table">
        <thead>
        <tr>
          <th>Universe Name</th>
          <th>Creation Date</th>
          <th>Availability Zone</th>
          <th>Regions</th>
          <th>Provider</th>
        </tr>
        </thead>
        <tbody>
        </tbody>
      </table>
       <div>
         <div className="createUniverseButton btn btn-default btn-success" onClick={this.showCreateInstanceModal}>Create Universe</div>
       </div>

        <Modal isOpen={this.state.isModalOpen} onRequestClose={this.closeModal}>
          <div className="modal-heading">
            <button type="button" className="close" data-dismiss="modal" aria-label="Close" onClick={this.closeModal}>
              <span aria-hidden="true">&times;</span>
            </button>
            <div className="text-center h4">Create A New Universe</div>
          </div>
          <div className="modal-body">
             <div className="modal-body-left">
               <form>

                 <label className="form-item-label">
                   Universe Name
                 <input className="form-control" placeholder="Universe Name" />
                 </label>

                 <label className="form-item-label">
                   Cloud Provider
                 <select className="form-control"></select>
                 </label>

                 <label className="form-item-label">
                   Region
                 <select className="form-control"></select>
                 </label>

                 <div className="createUniverseFormSplit">
                     Advanced
                 </div>

                 <label className="form-item-label">
                 Multi AZ Capable &nbsp;
                 <input type="checkbox" />
                 </label>

                 <label className="form-item-label">
                   Instance Type
                 <select className="form-control"></select>
                 </label>
               </form>
             </div>
             <div className="modal-body-right">
                <div className="body-right-content">
                  <div className="content-head h4">
                    Universe Overview
                  </div>

                  <div className="content-item">
                    Universe Name :
                  </div>
                  <div className="content-item">
                    Universe Map:
                  </div>
                  <div className="content-item">
                    Cost Estimate:
                  </div>
                 </div>
               <div className="createButton btn btn-default btn-success">Create Universe</div>
             </div>
          </div>
        </Modal>
        
      </div>
    );
  }
}
