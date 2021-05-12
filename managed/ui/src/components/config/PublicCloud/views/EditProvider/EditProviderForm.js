// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Alert } from 'react-bootstrap';
import { YBButton, YBTextInputWithLabel } from '../../../../common/forms/fields';
import { Field } from 'redux-form';
import { isNonEmptyObject, isNonEmptyString } from '../../../../../utils/ObjectUtils';
import { getPromiseState } from '../../../../../utils/PromiseUtils';
import { getClusterProviderUUIDs } from '../../../../../utils/UniverseUtils';

export default class EditProviderForm extends Component {
  constructor(props) {
    super(props);
    this.state = {
      providerInUse: new Set()
    };
  }
  submitEditProvider = (payload) => {
    this.props.submitEditProvider(payload);
  };

  switchToResultView = () => {
    this.props.switchToResultView();
  };

  componentDidUpdate(prevProps) {
    const { editProvider } = this.props;
    if (
      isNonEmptyObject(editProvider) &&
      getPromiseState(editProvider).isSuccess() &&
      getPromiseState(prevProps.editProvider).isLoading()
    ) {
      this.props.reloadCloudMetadata();
      this.props.switchToResultView();
    }
  }

  componentDidMount() {
    this.props.fetchUniverseList();
  }

  shouldComponentUpdate(nextProps) {
    const { providerInUse } = this.state;
    if (
      nextProps.universeList.data &&
      nextProps.universeList.data.length !== 0 &&
      providerInUse.size === 0
    ) {
      nextProps.universeList.data.forEach((universe) => {
        const [primaryClusterProviderUUID, readOnlyClusterProviderUUID] = getClusterProviderUUIDs(
          universe.universeDetails.clusters
        );
        if (primaryClusterProviderUUID) {
          providerInUse.add(primaryClusterProviderUUID);
        }
        if (readOnlyClusterProviderUUID) {
          providerInUse.add(readOnlyClusterProviderUUID);
        }
      });
      this.setState({ providerInUse });
    }
    return true;
  }

  render() {
    const { error, handleSubmit, hostedZoneId, uuid } = this.props;
    const { providerInUse } = this.state;
    let isHostedZoneIdValid = true;
    let verifyEditConditions = true;
    if (!isNonEmptyString(hostedZoneId)) {
      isHostedZoneIdValid = false;
      verifyEditConditions = false;
    }
    return (
      <div>
        <h4>Edit Provider</h4>
        <form name="EditProviderForm" onSubmit={handleSubmit(this.submitEditProvider)}>
          {error && <Alert bsStyle="danger">{error}</Alert>}
          <div className="aws-config-form form-right-aligned-labels">
            <Field
              name="accountName"
              type="text"
              label="Name"
              component={YBTextInputWithLabel}
              isReadOnly={providerInUse.has(uuid)}
            />
            <Field
              name="accountUUID"
              type="text"
              label="Provider UUID"
              component={YBTextInputWithLabel}
              isReadOnly={true}
            />
            <Field
              name="secretKey"
              type="text"
              label="SSH Key"
              component={YBTextInputWithLabel}
              isReadOnly={providerInUse.has(uuid)}
            />
            <Field
              name="hostedZoneId"
              type="text"
              label="Hosted Zone ID"
              component={YBTextInputWithLabel}
              isReadOnly={isHostedZoneIdValid || providerInUse.has(uuid)}
            />
            <div className="form-action-button-container">
              <YBButton
                btnText={'Submit'}
                btnClass={'btn btn-default save-btn'}
                btnType="submit"
                disabled={verifyEditConditions && providerInUse.has(uuid)}
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
