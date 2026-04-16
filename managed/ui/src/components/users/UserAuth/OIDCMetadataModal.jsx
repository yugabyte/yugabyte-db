import { useState } from 'react';
import AceEditor from 'react-ace';
import * as ace from 'ace-builds/src-noconflict/ace';
import { makeStyles } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { YBButton, YBModal } from '../../../redesign/components';
//Icons
import 'ace-builds/src-noconflict/theme-textmate';
import 'ace-builds/src-noconflict/mode-json';


ace.config.set('basePath', 'https://cdn.jsdelivr.net/npm/ace-builds@1.4.3/src-noconflict/');
ace.config.setModuleUrl(
  'ace/mode/json_worker',
  'https://cdn.jsdelivr.net/npm/ace-builds@1.4.3/src-noconflict/worker-json.js'
);

const useStyles = makeStyles((theme) => ({
  editorStyle: {
    height: 530,
    width: '100%',
    marginBottom: '32px',
    display: 'flex',
    border: '1px solid #CFCFD8',
    borderRadius: '6px',
    marginTop: '18px'
  },
  link: {
    textDecoration: 'underline',
    color: theme.palette.ybacolors.labelBackground
  }
}));

const OIDCMetadataModal = ({ value, open, onSubmit, onClose }) => {

  const { t } = useTranslation('translation', {
    keyPrefix: 'userAuth.OIDC.OIDCMetadataModal'
  });
  const classes = useStyles();

  const [editorValue, seteditorValue] = useState(value);
  const [editorErrors, setEditorErrors] = useState([]);

  return (
    <YBModal
      title={t('title')}
      open={open}
      submitLabel={t('apply', { keyPrefix: 'common' })}
      buttonProps={{
        primary: {
          disabled: editorErrors.length > 0
        }
      }}
      footerAccessory={<YBButton type='reset' variant="secondary" onClick={() => onClose()}>{t('cancel', { keyPrefix: 'common' })}</YBButton>}
      onClose={onClose}
      overrideHeight="820px"
      overrideWidth="750px"
      onSubmit={() => onSubmit(editorValue)}
    >
      <div >
        <Trans i18nKey="helpText" t={t} components={{ 'b': <b />, 'br': <br />, 'a': <a className={classes.link} href='#' /> }} />
        <br />
        <AceEditor
          mode="json"
          theme="textmate"
          id="json-editor-oidc-metadata"
          name="oidcProviderMetadata"
          showPrintMargin={false}
          fontSize={12}
          showGutter={true}
          width='100%'
          highlightActiveLine={true}
          setOptions={{
            showLineNumbers: true
          }}
          className={classes.editorStyle}
          onChange={(val) => seteditorValue(val)}
          onValidate={(errors) => setEditorErrors(errors)}
          value={editorValue}
        />
      </div>
    </YBModal >
  );
};

export default OIDCMetadataModal;
