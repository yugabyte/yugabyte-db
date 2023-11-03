// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold the backup config controls i.e Save,
// Update and Cancel.

import { YBButton } from '../../common/forms/fields';

export const ConfigControls = (props) => {
  const { activeTab, editView, listView, showListView } = props;

  return (
    <div className="form-action-button-container">
      {!listView[activeTab] && !editView[activeTab]?.isEdited && (
        <YBButton btnText="Save" btnClass="btn btn-orange" btnType="submit" />
      )}

      {editView[activeTab]?.isEdited && (
        <YBButton btnText="Update" btnClass="btn btn-orange" btnType="submit" />
      )}

      {(!listView[activeTab] || editView[activeTab]?.isEdited) && (
        <YBButton btnText="Cancel" btnClass="btn" onClick={showListView} />
      )}
    </div>
  );
};
