// Copyright (c) YugaByte, Inc.

import React, { useEffect, useMemo } from 'react';
import { isDefinedNotNull, isNonEmptyObject } from '../../utils/ObjectUtils';
import { showOrRedirect } from '../../utils/LayoutUtils';
import AceEditor from 'react-ace';
import 'ace-builds/src-noconflict/ext-searchbox';
import 'ace-builds/src-noconflict/mode-java';
import 'ace-builds/src-noconflict/theme-github';

const YugawareLogs = ({ currentCustomer, yugawareLogs, getLogs, logError }) => {
  const editorStyle = {
    width: '100%',
    height: 'calc(100vh - 150px)'
  };

  const logContent = useMemo(() => {
    // Hard limit set so even if 50k lines we show 10k lines only
    // When pagination for logs implemented this hard limit can be removed .
    const hardLimit = 10000;

    if (isDefinedNotNull(yugawareLogs) && isNonEmptyObject(yugawareLogs)) {
      return yugawareLogs
        .slice(yugawareLogs.length - hardLimit)
        .join('\n');
    } else {
      return 'Loading ...';
    }
  }, [yugawareLogs]);

  useEffect(() => {
    showOrRedirect(currentCustomer.data.features, 'main.logs');
    getLogs(); // call to get logs
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <div>
      <h2 className="content-title">
        <b>YugaWare logs</b>
      </h2>
      <div>
        {logError ? (
          <div>Something went wrong fetching logs.</div>
        ) : (
          <AceEditor
            theme="github"
            mode="java"
            name="dc-config-val"
            value={logContent}
            style={editorStyle}
            readOnly
            showPrintMargin={false}
            wrapEnabled={true}
          />
        )}
      </div>
    </div>
  );
};

export default YugawareLogs;
