import { useState } from 'react';
import AceEditor from 'react-ace';
import * as ace from 'ace-builds/src-noconflict/ace';
//Icons
import Bulb from '../images/bulb.svg';
import 'ace-builds/src-noconflict/theme-textmate';
import 'ace-builds/src-noconflict/mode-json';
import { FlexShrink } from '../../common/flexbox/YBFlexBox';
//Styles
import './UniverseForm.scss';

ace.config.set('basePath', 'https://cdn.jsdelivr.net/npm/ace-builds@1.4.3/src-noconflict/');
ace.config.setModuleUrl(
  'ace/mode/json_worker',
  'https://cdn.jsdelivr.net/npm/ace-builds@1.4.3/src-noconflict/worker-json.js'
);

const editorStyle = {
  height: 530,
  width: '100%',
  marginBottom: '32px',
  display: 'flex',
  border: '1px solid #CFCFD8',
  borderRadius: '6px'
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
    <div className="gflag-editor-container">
      <FlexShrink className="info-msg align-end">
        <img alt="--" src={Bulb} width="24" />
        &nbsp; &nbsp;
        <span className="bold-600">Make sure to use valid JSON formating: &nbsp;</span>{' '}
        <span className="muted-text-2">{`{ "FlagName" : "Value" , "FlagName" : "Value" }`}</span>
      </FlexShrink>
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
