import React, { useEffect, useState } from 'react';
import { Field } from 'formik';
import { ListGroupItem, ListGroup, Row, Col, Badge } from 'react-bootstrap';
import { YBButton, YBFormInput, YBInputField } from '../../common/forms/fields';
import { YBLabel } from '../../common/descriptors';
import { YBLoading } from '../../common/indicators';
import { FlexShrink, FlexContainer } from '../../common/flexbox/YBFlexBox';
import { fetchGFlags, fetchParticularFlag } from '../../../actions/universe';
//Icons
import Bulb from '../images/bulb.svg';
import BookOpen from '../images/book_open.svg';

//modes
const EDIT = 'EDIT';

const AddGFlag = ({ formProps, gFlagProps }) => {
  const { mode, server, dbVersion } = gFlagProps;
  const [searchVal, setSearchVal] = useState('');
  const [isLoading, setLoader] = useState(true);
  const [toggleMostUsed, setToggleMostUsed] = useState(false);
  const [allGFlagsArr, setAllGflags] = useState(null);
  const [mostUsedArr, setMostUsedFlags] = useState(null);
  const [filteredArr, setFilteredArr] = useState(null);
  const [selectedFlag, setSelectedFlag] = useState(null);

  //Declarative methods
  const filterByText = (arr, text) => arr.filter((e) => e?.name?.includes(text));

  const handleFlagSelect = (flag) => {
    let flagvalue = null;
    if (flag?.type === 'bool')
      if (['false', false].includes(flag?.default)) flagvalue = false;
      else flagvalue = true;
    else if (!['bool', 'string'].includes(flag?.type)) flagvalue = Number(flag?.default);
    else flagvalue = flag?.default;
    setSelectedFlag(flag);
    formProps.setValues({
      ...gFlagProps,
      flagname: flag?.name,
      flagvalue
    });
  };

  //custom methods
  const getAllFlags = async () => {
    try {
      const flags = await Promise.all([
        fetchGFlags(dbVersion, { server }), //ALl GFlags
        fetchGFlags(dbVersion, { server, mostUsedGFlags: true }) // Most used flags
      ]);
      setAllGflags(flags[0]?.data);
      setMostUsedFlags(flags[1]?.data);
      if (!toggleMostUsed) setFilteredArr(flags[0]?.data);
      else setFilteredArr(flags[1]?.data);
      setLoader(false);
    } catch (e) {
      console.error(e);
    }
  };

  const getFlagByName = async () => {
    try {
      const { flagname, flagvalue } = gFlagProps;
      const flag = await fetchParticularFlag(dbVersion, { server, name: flagname });
      setAllGflags([flag?.data]);
      setMostUsedFlags([flag?.data]);
      setFilteredArr([flag?.data]);
      setSelectedFlag(flag?.data);
      if (flagvalue === undefined)
        formProps.setValues({
          ...gFlagProps,
          flagvalue: flag?.data?.default
        });
      else formProps.setValues(gFlagProps);
      setLoader(false);
    } catch (e) {
      console.error(e);
    }
  };

  const onInit = () => {
    if (mode === EDIT) {
      getFlagByName();
    } else getAllFlags();
  };

  const onValueChanged = () => {
    if (!isLoading)
      setFilteredArr(filterByText(toggleMostUsed ? mostUsedArr : allGFlagsArr, searchVal));
  };

  //Effects
  useEffect(onValueChanged, [toggleMostUsed, searchVal]);
  useEffect(onInit, []);

  //nodes
  const valueLabel = (
    <>
      Flag Value &nbsp;
      <Badge className="gflag-badge">{gFlagProps?.server}</Badge>
    </>
  );

  const infoText = (
    <div className="info-msg">
      <img alt="--" src={Bulb} width="24" />
      &nbsp;
      <span>
        Start typing the Flag’s name in the search field above to find the Flag you are looking for
      </span>
    </div>
  );

  const documentationLink = (
    <Row>
      <img alt="--" src={BookOpen} width="12" />{' '}
      <a
        className="gflag-doc-link"
        rel="noopener noreferrer"
        href={`https://docs.yugabyte.com/latest/reference/configuration/yb-${server.toLowerCase()}/#${selectedFlag?.name
          ?.split('_')
          .join('-')}`}
        target="_blank"
      >
        More about this flag
      </a>
    </Row>
  );

  //renderers
  const renderFormComponent = (flag) => {
    switch (flag?.type) {
      case 'bool':
        return (
          <>
            <YBLabel label={valueLabel}>
              <div className="row-flex">
                {[true, false].map((target) => (
                  <span className="btn-group btn-group-radio mr-20" key={target}>
                    <Field
                      name={'flagvalue'}
                      type="radio"
                      component="input"
                      onChange={() => formProps.setFieldValue('flagvalue', target)}
                      value={`${target}`}
                      checked={`${target}` === `${formProps?.values['flagvalue']}`}
                    />{' '}
                    {`${target}`}{' '}
                    <span className="default-text">
                      {[target, `${target}`].includes(selectedFlag?.default) ? '(Default)' : ''}
                    </span>
                  </span>
                ))}
              </div>
            </YBLabel>
          </>
        );

      case 'string':
        return <Field name="flagvalue" type="text" label={valueLabel} component={YBFormInput} />;

      default:
        //number type
        return (
          <Field
            name="flagvalue"
            type="number"
            label={valueLabel}
            component={YBFormInput}
            step="any"
          />
        );
    }
  };

  const renderFlagList = () => (
    <>
      <FlexShrink>
        <YBInputField
          placeHolder="Search Flags"
          className="g-flag-search"
          onValueChanged={(text) => setSearchVal(text)}
        />
      </FlexShrink>
      <FlexShrink>
        <YBButton
          btnText="ALL FLAGS"
          disabled={mode === EDIT}
          btnClass={!toggleMostUsed ? 'btn btn-orange' : 'btn btn-default'}
          onClick={() => {
            if (toggleMostUsed) {
              setSelectedFlag(null);
              setToggleMostUsed(false);
            }
          }}
        />{' '}
        &nbsp;
        <YBButton
          btnText="MOST USED"
          disabled={mode === EDIT}
          btnClass={toggleMostUsed ? 'btn btn-orange' : 'btn btn-default'}
          onClick={() => {
            if (!toggleMostUsed) {
              setSelectedFlag(null);
              setToggleMostUsed(true);
            }
          }}
        />
      </FlexShrink>
      <div className="g-flag-list">
        <ListGroup>
          {(filteredArr || []).map((flag, i) => {
            const isSelected = flag.name === selectedFlag?.name;
            return (
              <ListGroupItem
                className={isSelected ? 'selected-gflag' : 'g-flag-list-item'}
                onClick={() => handleFlagSelect(flag)}
                key={flag.name}
              >
                {flag.name}
              </ListGroupItem>
            );
          })}
        </ListGroup>
      </div>
    </>
  );

  const renderFieldInfo = (title, description) => (
    <>
      <span className="gflag-description-title">{title}</span>
      <span className="gflag-description-value">{description}</span>
    </>
  );

  const renderFlagDetails = () => {
    if (selectedFlag)
      return (
        <>
          <div className="gflag-detail-container">
            <span className="flag-detail-header">Flag Details</span>
            {renderFieldInfo('Name', selectedFlag?.name)}
            {renderFieldInfo('Description', selectedFlag?.meaning)}
            <div className="gflag-detail-value">
              <FlexContainer direction="column">
                {selectedFlag?.default && (
                  <>
                    <span className="gflag-description-title">Default Value</span>
                    <Badge className="gflag-badge">{selectedFlag?.default}</Badge>
                    <br />
                  </>
                )}
                {documentationLink}
              </FlexContainer>
              {/* <FlexContainer direction="column">placeholder to show min and max values</FlexContainer> */}
            </div>
          </div>
          <div className="gflag-form">{renderFormComponent(selectedFlag)}</div>
        </>
      );
    else return infoText;
  };

  return (
    <div className="add-gflag-container">
      {isLoading ? (
        <div className="loading-container">
          <YBLoading />
        </div>
      ) : (
        <Row className="row-flex">
          <Col md={6} className="split-container">
            {renderFlagList()}
          </Col>
          <Col md={6} className="split-container add-border-left">
            {renderFlagDetails()}
          </Col>
        </Row>
      )}
    </div>
  );
};

export default AddGFlag;
