'use strict';

const X_DATA_CONTAINER = 'x-data-container';
const X_DATA_ITEM = 'x-data-item';
const X_DATA_ENTITIES_DESCRIPTION = 'x-data-entities-description';

function baseName(name) {
  if (typeof name !== 'string') {
    return name;
  }
  const t = name.trim();
  const i = t.lastIndexOf('.');
  return i === -1 ? t : t.slice(i + 1);
}

function alreadyExpanded(schema) {
  return (
    Array.isArray(schema.allOf) &&
    schema.allOf.length > 0 &&
    schema.allOf[0] &&
    schema.allOf[0].$ref &&
    schema.properties &&
    schema.properties.entities &&
    schema.properties.entities.items &&
    schema.properties.entities.items.$ref
  );
}

function hasOwn(obj, key) {
  return Object.prototype.hasOwnProperty.call(obj, key);
}

/**
 * Ensures x-data-container and x-data-item are only used as a pair of non-empty strings.
 * Reports via Redocly walk `report` so bundle fails with a clear message.
 */
function validateXDataPair(schema, { report }) {
  if (!schema || typeof schema !== 'object') {
    return;
  }
  const hasC = hasOwn(schema, X_DATA_CONTAINER);
  const hasI = hasOwn(schema, X_DATA_ITEM);
  if (!hasC && !hasI) {
    return;
  }
  if (hasC && !hasI) {
    report({
      message: `\`${X_DATA_ITEM}\` is required when \`${X_DATA_CONTAINER}\` is set (they must be used together).`,
    });
    return;
  }
  if (!hasC && hasI) {
    report({
      message: `\`${X_DATA_CONTAINER}\` is required when \`${X_DATA_ITEM}\` is set (they must be used together).`,
    });
    return;
  }
  const c = schema[X_DATA_CONTAINER];
  const i = schema[X_DATA_ITEM];
  if (typeof c !== 'string' || !c.trim()) {
    report({
      message: `\`${X_DATA_CONTAINER}\` must be a non-empty string when \`${X_DATA_ITEM}\` is set.`,
    });
    return;
  }
  if (typeof i !== 'string' || !i.trim()) {
    report({
      message: `\`${X_DATA_ITEM}\` must be a non-empty string when \`${X_DATA_CONTAINER}\` is set.`,
    });
    return;
  }
}

function expandSchema(schema) {
  if (!schema || typeof schema !== 'object') {
    return;
  }
  const c = schema[X_DATA_CONTAINER];
  const i = schema[X_DATA_ITEM];
  if (!c || !i || typeof c !== 'string' || typeof i !== 'string') {
    return;
  }
  if (alreadyExpanded(schema)) {
    return;
  }

  const superName = baseName(c);
  const entityName = baseName(i);
  if (!superName || !entityName) {
    return;
  }

  schema.type = schema.type || 'object';
  schema.allOf = [{ $ref: `./${superName}.yaml` }];

  const entities = {
    type: 'array',
    items: { $ref: `./${entityName}.yaml` },
  };
  const entDesc = schema[X_DATA_ENTITIES_DESCRIPTION];
  if (typeof entDesc === 'string' && entDesc.trim()) {
    entities.description = entDesc.trim();
  }

  if (!schema.properties) {
    schema.properties = {};
  }
  schema.properties.entities = entities;
}

function expandPagedSchemasPreprocessor() {
  return {
    Schema: {
      leave(schema, ctx) {
        validateXDataPair(schema, ctx);
        expandSchema(schema);
      },
    },
  };
}

// Redocly 1.x expects `id` on the exported object (not a factory return value).
module.exports = {
  id: 'x-data-container-expand',
  preprocessors: {
    oas3: {
      'expand-paged-schemas': expandPagedSchemasPreprocessor,
    },
  },
};
