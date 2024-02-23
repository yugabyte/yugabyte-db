import { useLocalStorage, useToggle } from "react-use";
import { YBCheckBox, YBModal } from "../components/common/forms/fields";
import ybLogo from '../components/common/YBLogo/images/yb_ybsymbol_dark.png';
import slackLogo from '../components/common/footer/images/slack-logo-full.svg';
import githubLogo from '../components/common/footer/images/github-light-small.png';
import tshirtImage from '../components/common/footer/images/tshirt-yb.png';

export const YBIntroDialog = () => {

  const [showIntroModal, setIntroDialogVal] = useLocalStorage('__yb_intro_dialog__', "new", { raw: true });
  const [dontShowInFuture, toggleDontShowInFuture] = useToggle(false);

  const introMessageStatus = (
    <div className="footer-accessory-wrapper">
      <YBCheckBox
        label={'Do not show this message in the future'}
        onClick={() => toggleDontShowInFuture(!dontShowInFuture)}
      ></YBCheckBox>
    </div>
  );

  const welcomeDialogTitle = (
    <div>
      Welcome to
      <img
        alt="YugaByte DB logo"
        className="social-media-logo"
        src={ybLogo}
        width="140"
        style={{ verticalAlign: 'text-bottom' }}
      />
    </div>
  );



  return (
    <YBModal
      title={welcomeDialogTitle}
      visible={showIntroModal === "new"}
      onHide={() => { setIntroDialogVal(dontShowInFuture ? 'hidden' : 'existing'); }}
      showCancelButton={true}
      cancelLabel={'Close'}
      footerAccessory={introMessageStatus}
    >
      <div className="intro-message-container">
        <a
          className="social-media-btn icon-end"
          href="https://www.yugabyte.com/slack"
          target="_blank"
          rel="noopener noreferrer"
        >
          <span>Join us on</span>
          <img alt="YugaByte DB Slack" src={slackLogo} width="65" />
        </a>
        <a
          className="social-media-btn icon-end"
          href="https://github.com/yugabyte/yugabyte-db"
          target="_blank"
          rel="noopener noreferrer"
        >
          <span>Star us on</span>
          <img
            alt="YugaByte DB GitHub"
            className="social-media-logo"
            src={githubLogo}
            width="18"
          />{' '}
          <b>GitHub</b>
        </a>
      </div>
      <div className="intro-message-container">
        <a
          className="social-media-btn"
          href="https://www.yugabyte.com/community-rewards"
          target="_blank"
          rel="noopener noreferrer"
        >
          <img alt="T-Shirt" src={tshirtImage} width="20" />
          <span>Get a Free t-shirt</span>
        </a>
        <a
          className="social-media-btn"
          href="https://docs.yugabyte.com"
          target="_blank"
          rel="noopener noreferrer"
        >
          <i className="fa fa-search" />
          <span>Read docs</span>
        </a>
      </div>
    </YBModal>
  );
};
