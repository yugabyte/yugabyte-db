/**
 * Make Cuuid (Customer UUID) optional and set default value to localStorage.getItem('customerId')
 */
module.exports = (inputSchema) => {
    return {
      ...inputSchema,
      paths: Object.entries(inputSchema.paths).reduce((acc, [path, pathItem]) => {
        return {
          ...acc,
          [`${path}`]: {
            ...pathItem,
            parameters: pathItem.parameters.map(param => ({
              ...param,
              required: param.name !== 'cUUID' && param.required,
              schema: {
                ...param.schema,
                default: param.name === 'cUUID' ? "localStorage.getItem('customerId')!" : param.schema.default
              }
            }))
          }
        };
      }, {}
      )
    };
  };
