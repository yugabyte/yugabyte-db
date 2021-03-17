// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)

import { render, unmountComponentAtNode } from "react-dom";
import { act } from "react-dom/test-utils";
import { StorageConfigTypes } from "./ConfigType";

let container = null;
beforeEach(() => {
  // setup a DOM element as a render target.
  container = document.createElement("div");
  document.body.appendChild(container);
});

afterEach(() => {
  // cleanup on exiting.
  unmountComponentAtNode(container);
  container.remove();
  container = null;
});

/**
 * This test case will run to check the number of input fields
 * for NFS tab.
 */
it("It should render only 2 input fields for NFS tab:", () => {
  let fieldLength = 0;
  act(() => {
    render(Object.keys(StorageConfigTypes).map((configName) => {
      const configTemplate = StorageConfigTypes[configName];
      if (configName === "NFS") {
        fieldLength = configTemplate.fields.length;
      }
    }), container);
  });
  expect(2).toBe(fieldLength);
});

/**
 * This test case will run to check the number of input fields
 * for GCS tab.
 */
it("It should render only 3 input fields for GCS tab:", () => {
  let fieldLength = 0;
  act(() => {
    render(Object.keys(StorageConfigTypes).map((configName) => {
      const configTemplate = StorageConfigTypes[configName];
      if (configName === "GCS") {
        fieldLength = configTemplate.fields.length;
      }
    }), container);
  });
  expect(3).toBe(fieldLength);
});

/**
 * This test case will run to check the number of input fields
 * for AZ tab.
 */
it("It should render only 3 input fields for AZ tab:", () => {
  let fieldLength = 0;
  act(() => {
    render(Object.keys(StorageConfigTypes).map((configName) => {
      const configTemplate = StorageConfigTypes[configName];
      if (configName === "AZ") {
        fieldLength = configTemplate.fields.length;
      }
    }), container);
  });
  expect(3).toBe(fieldLength);
});