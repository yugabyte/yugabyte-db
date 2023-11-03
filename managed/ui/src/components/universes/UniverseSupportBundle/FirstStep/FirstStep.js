import { YBButton } from '../../../common/forms/fields';
import {
  RbacValidator
} from '../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../redesign/features/rbac/ApiAndUserPermMapping';

export const FirstStep = ({ onCreateSupportBundle, universeUUID }) => {
  return (
    <div className="universe-support-bundle-step-one">
      <i className="fa fa-file-archive-o first-step-icon" aria-hidden="true" />
      <RbacValidator
        isControl
        accessRequiredOn={{
          ...ApiPermissionMap.CREATE_SUPPORT_BUNDLE, onResource: {
            "UNIVERSE": universeUUID
          }
        }}
        popOverOverrides={{ zIndex: 100000 }}
      >
        <YBButton
          variant="outline-dark"
          onClick={onCreateSupportBundle}
          btnText={
            <>
              <i className="fa fa-plus create-bundle-icon" aria-hidden="true" />
              Create Support Bundle
            </>
          }
        />
      </RbacValidator>
      <p className="subtitle-text">
        Support bundles contain the diagnostic information. This can include log files, config
        files, metadata and etc. You can analyze this information locally on your machine or send
        the bundle to Yugabyte Support team.
      </p>
    </div>
  );
};
