#!/usr/bin/env node
// Format a batch of OpenAPI fragment files in a single Node process.
//
// The previous approach shelled out to the `openapi-format` CLI once per file (via `npx`), paying
// node/npx startup (~1-2s) for each of the ~350 fragment files. The actual formatting work is only
// milliseconds, so startup dominated and made this the slowest part of the build. This script loads
// openapi-format's programmatic API once, reads the sort configuration once, and formats every file
// in-process - producing byte-for-byte identical output to the CLI while collapsing hundreds of
// process spawns into one.
//
// Usage:
//   node openapi_format_batch.js <sortFile> <sortComponentsFile> <listFile>
// where <listFile> is a newline-delimited list of OpenAPI YAML files to format (edited in place).

const fs = require('fs');
const os = require('os');
const path = require('path');

// Resolve openapi-format relative to this script so cwd does not matter (installed locally by
// openapi_format_install.sh into scripts/node_modules).
const openapiFormat = require(path.join(__dirname, 'node_modules', 'openapi-format'));

async function formatOne(file, sortSet, sortComponentsSet, tmpDir) {
  // openapi.yaml is the bundled entrypoint, not a fragment; the old per-file script skipped it.
  if (path.basename(file) === 'openapi.yaml') {
    return;
  }
  const options = { sort: true, lineWidth: -1, sortSet, sortComponentsSet };
  let resObj = await openapiFormat.parseFile(file);
  const resFormat = await openapiFormat.openapiSort(resObj, options);
  if (resFormat.data) resObj = resFormat.data;

  // Write to a temp file and only overwrite the original when the content actually changed, so
  // unchanged files keep their mtime (sbt uses file mtimes for incremental change detection).
  const tmpFile = path.join(tmpDir, file.replace(/[\\/]/g, '_'));
  await openapiFormat.writeFile(tmpFile, resObj, options);
  const formatted = fs.readFileSync(tmpFile);
  let current = null;
  try {
    current = fs.readFileSync(file);
  } catch (e) {
    // Original missing - fall through and write it.
  }
  if (current === null || !formatted.equals(current)) {
    fs.writeFileSync(file, formatted);
  }
  fs.unlinkSync(tmpFile);
}

async function main() {
  const [sortFile, sortComponentsFile, listFile] = process.argv.slice(2);
  if (!sortFile || !sortComponentsFile || !listFile) {
    console.error(
      'Usage: node openapi_format_batch.js <sortFile> <sortComponentsFile> <listFile>');
    process.exit(2);
  }
  const sortSet = await openapiFormat.parseFile(sortFile);
  const sortComponentsSet = await openapiFormat.parseFile(sortComponentsFile);
  const files = fs
    .readFileSync(listFile, 'utf8')
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => l.length > 0);

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'openapi-format-'));
  try {
    for (const file of files) {
      try {
        await formatOne(file, sortSet, sortComponentsSet, tmpDir);
      } catch (err) {
        console.error(`Error: Failed to format YML file: ${file}`);
        console.error(err);
        process.exit(1);
      }
    }
    console.log(`Formatted ${files.length} OpenAPI file(s).`);
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
