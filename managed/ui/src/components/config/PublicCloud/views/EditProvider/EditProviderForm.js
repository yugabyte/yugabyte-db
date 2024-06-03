// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel } from '../../../../common/forms/fields';
import { Field } from 'redux-form';
import { isNonEmptyObject, isNonEmptyString } from '../../../../../utils/ObjectUtils';
import { getPromiseState } from '../../../../../utils/PromiseUtils';
import { getClusterProviderUUIDs } from '../../../../../utils/UniverseUtils';
import { YBLoading } from '../../../../common/indicators';

export default class EditProviderForm extends Component {
  constructor(props) {
    super(props);
    this.state = {
      providersInUse: new Set()
    };
  }
  submitEditProvider = (payload) => {
    this.props.submitEditProvider(payload);
  };

  switchToResultView = () => {
    this.props.switchToResultView();
  };

  componentDidUpdate(prevProps) {
    const { editProvider, universeList, reloadCloudMetadata, switchToResultView } = this.props;
    const { providersInUse } = this.state;
    if (isNonEmptyObject(editProvider)) {
      if (
        getPromiseState(editProvider).isSuccess() &&
        getPromiseState(prevProps.editProvider).isLoading()
      ) {
        reloadCloudMetadata();
        switchToResultView();
      }
    }

    if (universeList.data && universeList.data.length !== 0 && providersInUse.size === 0) {
      universeList.data.forEach((universe) => {
        const [primaryClusterProviderUUID, readOnlyClusterProviderUUID] = getClusterProviderUUIDs(
          universe.universeDetails.clusters
        );
        if (primaryClusterProviderUUID) {
          providersInUse.add(primaryClusterProviderUUID);
        }
        if (readOnlyClusterProviderUUID) {
          providersInUse.add(readOnlyClusterProviderUUID);
        }
      });
      this.setState({ providersInUse: new Set(providersInUse) });
    }
  }

  componentDidMount() {
    if (!getPromiseState(this.props.universeList).isSuccess()) {
      this.props.fetchUniverseList();
    }
  }

  render() {
    const { error, handleSubmit, hostedZoneId, uuid, universeList } = this.props;
    const { providersInUse } = this.state;
    let isHostedZoneIdValid = true;
    let verifyEditConditions = true;

    if (!isNonEmptyString(hostedZoneId)) {
      isHostedZoneIdValid = false;
      verifyEditConditions = false;
    }

    if (getPromiseState(universeList).isLoading()) {
      return <YBLoading />;
    }

    return (
      <div data-testid="edit-provider-form">
        <h4>Edit Provider</h4>
        <form name="EditProviderForm" onSubmit={handleSubmit(this.submitEditProvider)}>
          {error && <Alert bsStyle="danger">{error}</Alert>}
          <div className="aws-config-form form-right-aligned-labels">
            <Field
              name="accountName"
              type="text"
              label="Name"
              component={YBTextInputWithLabel}
              isReadOnly={providersInUse.has(uuid)}
            />
            <Field
              name="accountUUID"
              type="text"
              label="Provider UUID"
              component={YBTextInputWithLabel}
              isReadOnly={true}
            />
            <Field
              ariaLabel="secretKey"
              name="secretKey"
              type="text"
              label="SSH Key"
              component={YBTextInputWithLabel}
              isReadOnly={providersInUse.has(uuid)}
            />
            <Field
              name="hostedZoneId"
              type="text"
              label="Hosted Zone ID"
              component={YBTextInputWithLabel}
              isReadOnly={isHostedZoneIdValid || providersInUse.has(uuid)}
            />
            <div className="form-action-button-container">
              <YBButton
                btnText={'Submit'}
                btnClass={'btn btn-default save-btn'}
                btnType="submit"
                disabled={verifyEditConditions && providersInUse.has(uuid)}
              />
              <YBButton
                btnText={'Cancel'}
                btnClass={'btn btn-default'}
                onClick={this.switchToResultView}
              />
            </div>
          </div>
        </form>
      </div>
    );
  }
}
