import React, { useState } from 'react';
import AceEditor from 'react-ace';

import 'ace-builds/src-noconflict/theme-textmate';
import 'ace-builds/src-noconflict/mode-json';
import 'ace-builds/src-noconflict/mode-text';
import 'ace-builds/src-min-noconflict/snippets/json';

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
        enableSnippets={true}
        setOptions={{
          showLineNumbers: true,
          useWorker: false
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
