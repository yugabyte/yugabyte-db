// Copyright (c) YugaByte, Inc.

import { useState } from 'react';
import { YBButton } from '../common/forms/fields';
import { Highlighter } from '../../helpers/Highlighter';
import copy from 'copy-to-clipboard';

const statsTextMap = {
  nodeName: 'Node Name',
  privateIp: 'Private IP',
  keyspace: 'Keyspace',
  dbName: 'DB Name',
  sessionStatus: 'Session Status',
  elapsedMillis: 'Elapsed Time',
  type: 'Type',
  clientHost: 'Client Host',
  clientPort: 'Client Port',
  queryStartTime: 'Query Start',
  appName: 'Client Name',
  calls: 'Count',
  datname: 'DB',
  local_blks_hit: 'Tmp tables RAM',
  local_blks_written: 'Tmp tables On Disk',
  max_time: 'Max Exec Time',
  mean_time: 'Avg Exec Time',
  min_time: 'Min Exec Time',
  rows: 'Rows',
  stddev_time: 'Stdev Exec Time',
  total_time: 'Total Time',
  rolname: 'User',
  userid: 'User Id'
};

export const LegacyQueryInfoSidePanel = ({ queryData, visible, onHide }) => {
  const [copied, setCopied] = useState(false);

  const handleCopy = () => {
    copy(queryData.query);
    setCopied(true);
    setTimeout(() => {
      setCopied(false);
    }, 2000);
  };
  return (
    <div className={`side-panel ${!visible ? 'panel-hidden' : ''}`}>
      <div className="side-panel__header">
        <span className="side-panel__icon--close" onClick={onHide}>
          <i className="fa fa-close" />
        </span>
        <div className="side-panel__title">DETAILS</div>
      </div>
      <div className="side-panel__content">
        {queryData && (
          <div className="side-panel__query">
            <Highlighter type="sql" text={queryData.query} element="pre" />
          </div>
        )}
        <div className="copy-btn-container">
          <YBButton
            btnText={copied ? 'Copied!' : 'Copy Statement'}
            btnIcon="fa fa-copy"
            onClick={handleCopy}
          />
        </div>
        <ul className="side-panel__details">
          {queryData &&
            Object.keys(queryData).map((key) => {
              if (key !== 'id' && key !== 'queryid' && key !== 'query') {
                return (
                  <li key={`details-${key}`}>
                    <label>
                      <strong>{statsTextMap[key]}</strong>
                    </label>
                    <span>{queryData[key]}</span>
                  </li>
                );
              }
              return null;
            })}
        </ul>
      </div>
    </div>
  );
};
