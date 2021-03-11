// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold the backup config controls i.e Save,
// Update and Cancel.

import React from "react";
import { YBButton } from "../../common/forms/fields";

const ConfigControls = (props) => {
  const {
    activeTab,
    editView,
    editView: {
      [activeTab]: { data }
    },
    listView,
    showListView
  } = props;

  return (
    <div className="form-action-button-container">
      {!listView[activeTab] &&
        !editView[activeTab].isEdited &&
        <YBButton
          btnText="Save"
          btnClass="btn btn-orange"
          btnType="submit"
        />
      }

      {editView[activeTab].isEdited &&
        <YBButton
          btnText="Update"
          btnClass="btn btn-orange"
          btnType="submit"
        />
      }

      {(!listView[activeTab] ||
        editView[activeTab].isEdited) &&
        <YBButton
          btnText="Cancel"
          btnClass="btn btn-orange"
          onClick={showListView}
        />
      }
    </div>
  )
}

export { ConfigControls }