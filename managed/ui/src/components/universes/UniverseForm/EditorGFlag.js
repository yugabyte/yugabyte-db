import React, { useState } from 'react';
import AceEditor from 'react-ace';
import 'ace-builds/src-noconflict/theme-textmate';
import 'ace-builds/src-noconflict/mode-json';
const ace = require('ace-builds/src-noconflict/ace');
ace.config.set('basePath', 'https://cdn.jsdelivr.net/npm/ace-builds@1.4.3/src-noconflict/');
ace.config.setModuleUrl(
  'ace/mode/json_worker',
  'https://cdn.jsdelivr.net/npm/ace-builds@1.4.3/src-noconflict/worker-json.js'
);

const editorStyle = {
  height: '700px',
  width: '100%',
  marginBottom: '20px'
};

const EditorGFlag = ({ formProps, gFlagProps }) => {
  const [editorValue, seteditorValue] = useState(JSON.stringify({}));
  const handleBlur = () => {
    formProps.setValues({
      flagvalue: editorValue,
      ...gFlagProps
    });
  };

  return (
    <div className="col-lg-12">
      <AceEditor
        mode="json"
        theme="textmate"
        id="json-editor-flagvalue"
        name="flagvalue"
        showPrintMargin={false}
        fontSize={12}
        showGutter={true}
        highlightActiveLine={true}
        setOptions={{
          showLineNumbers: true
        }}
        style={editorStyle}
        onChange={(val) => seteditorValue(val)}
        onBlur={handleBlur}
        value={editorValue}
      />
    </div>
  );
};

export default EditorGFlag;
