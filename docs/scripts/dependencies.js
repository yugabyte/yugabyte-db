/**
 * Dynamically add Hugo module dependencies and FA Pro SCSS if installed.
 */
const fs = require('fs');
const path = require('path');

const projectRoot = process.cwd();

/**
 * Check whether the package is installed or not.
 *
 * @param {String} packageName The full package name, including scope (e.g., '@fortawesome/fontawesome-pro').
 *
 * @returns boolean
 */
function isPackageInstalled(packageName) {
  const packagePath = path.join(projectRoot, 'node_modules', packageName);

  return fs.existsSync(packagePath);
}

/**
 * Removes leading comments and blank lines from a TOML file content.
 * A comment line starts with '#'. Blank lines are also removed if they appear
 * before the first non-comment, non-blank line.
 *
 * @param {string} content The raw content of the TOML file.
 *
 * @returns {string} The content with leading comments and blank lines removed.
 */
function removeLeadingTomlComments(content) {
  const lines = content.split('\n');
  let firstNonCommentLineIndex = 0;

  // Find the index of the first line that is not a comment or blank
  while (firstNonCommentLineIndex < lines.length) {
    const line = lines[firstNonCommentLineIndex].trim();
    if (line.length > 0 && !line.startsWith('#')) {
      break; // Found the first non-comment, non-blank line
    }
    firstNonCommentLineIndex++;
  }

  // Join the lines from the first non-comment line onwards
  return lines.slice(firstNonCommentLineIndex).join('\n');
}

/**
 * Copy the content of the predefined SCSS versions based on whether FA Pro
 * is installed or not.
 */
function setFontAwesomeScss() {
  const defaultFilePath = path.join(projectRoot, 'assets', 'scss', 'fontawesome', '_default.scss');
  const freeFilePath = path.join(projectRoot, 'assets', 'scss', 'fontawesome', '_free.scss');
  const proFilePath = path.join(projectRoot, 'assets', 'scss', 'fontawesome', '_pro.scss');

  const isFontAwesomeProInstalled = isPackageInstalled('@fortawesome/fontawesome-pro');
  let sourceFilePath;
  if (isFontAwesomeProInstalled) {
    sourceFilePath = proFilePath;
    // console.log(`Condition met: Copying SCSS content from ${proFilePath}`);
  } else {
    sourceFilePath = freeFilePath;
    // console.log(`Condition met: Copying SCSS content from ${freeFilePath}`);
  }

  try {
    const data = fs.readFileSync(sourceFilePath, 'utf8');
    fs.writeFileSync(defaultFilePath, data);
    // console.log(`SCSS content copied successfully to ${defaultFilePath}.`);
  } catch (err) {
    console.error(`Error processing SCSS fontawesome file ${sourceFilePath}:`, err.message);
    throw err;
  }
}

/**
 * Manages the module.toml file in _default by copying and appending content
 * from the _modules directory.
 *
 * Pre-condition:
 * - `config/_modules/module.toml` exists and does NOT contain a top-level [module] header.
 * - `config/_modules/fa.module.toml` exists and does NOT contain a top-level [module] header.
 */
function manageHugoModuleToml() {
  const defaultConfigsDir = path.join(projectRoot, 'config', '_default');
  const defaultModulePath = path.join(defaultConfigsDir, 'module.toml');

  const modulesSourceDir = path.join(projectRoot, 'config', '_modules');
  const baseModuleSourcePath = path.join(modulesSourceDir, 'module.toml');
  const faModuleSourcePath = path.join(modulesSourceDir, 'fa.module.toml');

  try {
    /*
     * Delete existing module.toml in _default.
     */
    try {
      if (fs.existsSync(defaultModulePath)) {
        fs.unlinkSync(defaultModulePath);
        // console.log(`Existing ${defaultModulePath} deleted.`);
      }
    } catch (err) {
      console.warn(`Warning: Could not delete ${defaultModulePath}:`, err.message);
    }

    /*
     * Copy content from base module.toml file to create the new target file.
     * Remove leading comments before writing.
     */
    try {
      const baseContent = fs.readFileSync(baseModuleSourcePath, 'utf8');
      const cleanedBaseContent = removeLeadingTomlComments(baseContent);
      fs.writeFileSync(defaultModulePath, cleanedBaseContent);
      // console.log(`Copied cleaned ${baseModuleSourcePath} to create ${defaultModulePath}.`);
    } catch (err) {
      console.error(`Error: Could not copy ${baseModuleSourcePath} to ${defaultModulePath}. Make sure the source file exists and permissions are correct.`, err.message);
      throw err;
    }

    /*
     * If Font Awesome Pro is installed, append fa.module.toml content.
     * Remove leading comments before appending.
     */
    const isFontAwesomeProInstalled = isPackageInstalled('@fortawesome/fontawesome-pro');
    if (isFontAwesomeProInstalled) {
      try {
        if (fs.existsSync(faModuleSourcePath)) {
          const faContent = fs.readFileSync(faModuleSourcePath, 'utf8');
          const cleanedFaContent = removeLeadingTomlComments(faContent);

          fs.appendFileSync(defaultModulePath, '\n' + cleanedFaContent);
          // console.log(`Appended cleaned ${faModuleSourcePath} to ${defaultModulePath}.`);
        } else {
          console.warn(`Warning: Font Awesome Pro is installed but ${faModuleSourcePath} not found. Skipping append.`);
        }
      } catch (err) {
        console.error(`Error appending ${faModuleSourcePath}:`, err.message);
        throw err;
      }
    }

  } catch (err) {
    console.error(`Fatal error managing module.toml:`, err.message);
    throw err;
  }
}

try {
  manageHugoModuleToml();
  setFontAwesomeScss();
} catch (error) {
  console.error('Configuration setup failed, exiting...', error);
  process.exit(1);
}
