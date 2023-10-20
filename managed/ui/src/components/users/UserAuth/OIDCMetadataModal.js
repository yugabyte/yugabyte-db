import { useState } from 'react';
import AceEditor from 'react-ace';
import { YBModal } from '../../common/forms/fields';
//Icons
import 'ace-builds/src-noconflict/theme-textmate';
import 'ace-builds/src-noconflict/mode-json';

import * as ace from 'ace-builds/src-noconflict/ace';

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

const OIDCMetadataModal = ({ value, open, onSubmit, onClose }) => {
  const [editorValue, seteditorValue] = useState(value);
  const [editorErrors, setEditorErrors] = useState([]);

  return (
    <YBModal
      title="Configure OIDC Provider Metadata"
      visible={open}
      showCancelButton={true}
      submitLabel="Apply"
      cancelLabel="Cancel"
      cancelBtnProps={{
        className: 'btn btn-default pull-left oidc-cancel-btn'
      }}
      disableSubmit={editorErrors.length > 0}
      onHide={onClose}
      onFormSubmit={() => onSubmit(editorValue)}
    >
      <div className="oidc-modal-c">
        <AceEditor
          mode="json"
          theme="textmate"
          id="json-editor-oidc-metadata"
          name="oidcProviderMetadata"
          showPrintMargin={false}
          fontSize={12}
          showGutter={true}
          highlightActiveLine={true}
          setOptions={{
            showLineNumbers: true
          }}
          style={editorStyle}
          onChange={(val) => seteditorValue(val)}
          onValidate={(errors) => setEditorErrors(errors)}
          value={editorValue}
        />
      </div>
    </YBModal>
  );
};

export default OIDCMetadataModal;
