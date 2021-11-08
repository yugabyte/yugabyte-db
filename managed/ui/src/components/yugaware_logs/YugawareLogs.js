// Copyright (c) YugaByte, Inc.

import React, { useState } from 'react';
import { showOrRedirect } from '../../utils/LayoutUtils';
import AceEditor from 'react-ace';
import 'ace-builds/src-noconflict/ext-searchbox';
import 'ace-builds/src-noconflict/mode-java';
import 'ace-builds/src-noconflict/theme-github';
import { Col, Row } from 'react-bootstrap';
import { YBControlledNumericInput, YBInputField, YBSelectWithLabel } from '../common/forms/fields';
import { useDebounce, useMount } from 'react-use';

const YugawareLogs = ({ currentCustomer, yugawareLogs, getLogs, logError, fetchUniverseList }) => {
  const editorStyle = {
    width: '100%',
    height: 'calc(100vh - 150px)'
  };

  const [maxLines, setMaxLines] = useState(100);
  const [regex, setRegex] = useState(undefined);
  const [selectedUniverse, setSelectedUniverse] = useState(undefined);
  const [universeList, setUniverseList] = useState([<option key={'loading'}>Loading..</option>]);

  useDebounce(
    () => {
      getLogs(maxLines, regex, selectedUniverse);
    },
    500,
    [maxLines, regex, selectedUniverse]
  );

  useMount(() => {
    showOrRedirect(currentCustomer.data.features, 'main.logs');
    getLogs(maxLines, regex, selectedUniverse);
    fetchUniverseList().then((resp) => {
      const universesOptions = resp.map((uni) => (
        <option key={uni.universeUUID} value={uni.name}>
          {uni.name}
        </option>
      ));
      setUniverseList([
        <option value={''} key={'default'}>
          Select a universe
        </option>,
        ...universesOptions
      ]);
    });
  });

  return (
    <div>
      <h2 className="content-title">
        <b>YugaWare logs</b>
      </h2>
      <Row>
        <Col lg={8}>
          <YBInputField
            placeHolder="Enter your regex here"
            onValueChanged={(val) => setRegex(val)}
          />
        </Col>
        <Col lg={2}>
          <YBSelectWithLabel
            options={universeList}
            onInputChanged={(value) => setSelectedUniverse(value ? value : undefined)}
          />
        </Col>
        <Col lg={2}>
          <YBControlledNumericInput
            placeHolder="max lines"
            minVal={10}
            val={maxLines}
            onInputChanged={setMaxLines}
          />
        </Col>
      </Row>

      <div>
        {logError ? (
          <div>Something went wrong fetching logs.</div>
        ) : (
          <Row>
            <Col lg={12}>
              <AceEditor
                theme="github"
                mode="java"
                name="dc-config-val"
                value={yugawareLogs || 'Loading...'}
                style={editorStyle}
                readOnly
                showPrintMargin={false}
                wrapEnabled={true}
              />
            </Col>
          </Row>
        )}
      </div>
    </div>
  );
};

export default YugawareLogs;
