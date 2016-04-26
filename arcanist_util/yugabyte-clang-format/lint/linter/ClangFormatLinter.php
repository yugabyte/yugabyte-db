<?php

/**
 * Uses git-clang-format to lint and autofix your C/C++ code.
 */
final class ClangFormatLinter extends ArcanistExternalLinter {

  public function getInfoName() {
    return 'git-clang-format';
  }

  public function getInfoURI() {
    return 'https://llvm.org/svn/llvm-project/cfe/trunk/tools/clang-format/git-clang-format';
  }

  public function getInfoDescription() {
    return pht('Use git-clang-format for processing specified files.');
  }

  public function getLinterName() {
    return 'git-clang-format';
  }

  public function getLinterConfigurationName() {
    return 'git-clang-format';
  }

  public function getLinterConfigurationOptions() {
    $options = array(
    );

    return $options + parent::getLinterConfigurationOptions();
  }

  public function getDefaultBinary() {
    return 'git-clang-format';
  }

  public function getInstallInstructions() {
    return pht('Download git-clang-format through ' .
      '`wget https://llvm.org/svn/llvm-project/cfe/trunk/tools/clang-format/git-clang-format`\n' .
      'Make sure you add it to your $PATH.');
  }

  public function shouldExpectCommandErrors() {
    return false;
  }

  protected function getMandatoryFlags() {
    // If we ever find ourselves having issues with the fixes, we could run this interactively,
    // using --patch, where you could y/n individual fixes. However, arc lint doesn't have an
    // interactive-friendly mode.
    //
    // For now, we also do not get an actual git hash trickled down to the linter, so we cannot
    // run over the full set of differences in your whole diff, but rather over your latest set
    // of changes. As such, we're hardcoding HEAD^ here as the diff point, but we should probably
    // setup a simple wrapper script that sets up the commit hash accordingly. Tracking in ENG-83.
    return array(
      "--diff",
      "--commit",
      "HEAD^"
    );
  }

  protected function generateMessage($path, $diff_group_metadata) {
    $message = id(new ArcanistLintMessage())
      ->setPath($path)
      ->setLine($diff_group_metadata->old_line_no + $diff_group_metadata->context_lines)
      ->setChar(1)
      ->setGranularity(ArcanistLinter::GRANULARITY_FILE)
      ->setCode('ClangFormat')
      ->setSeverity(ArcanistLintSeverity::SEVERITY_WARNING)
      ->setName('Code style violation')
      ->setDescription("'$path' has code style errors.")
      ->setOriginalText(implode($diff_group_metadata->old_lines, "\n"))
      ->setReplacementText(implode($diff_group_metadata->new_lines, "\n"));
    return $message;
  }

  protected function parseLinterOutput($path, $err, $stdout, $stderr) {
    if ($err !== 0) {
      return false;
    }

    $intro_lines_to_skip = 4;
    $first_diff_gruop = true;

    $messages = array();
    $diff_group_metadata = new DiffGroupMetadata();

    $lines = phutil_split_lines($stdout, false);
    foreach ($lines as $line) {
      // skip header
      if ($intro_lines_to_skip > 0) {
        $intro_lines_to_skip -= 1;
        continue;
      }

      $matches = null;

      // diff meta
      $match = preg_match(
        '/^@@ -(?<old_line_no>[0-9]+),(?<old_char_no>[0-9]+) '.
        '\+(?<new_line_no>[0-9]+),(?<new_char_no>[0-9]+) @@.*$/',
        $line,
        $matches);

      if ($match) {
        if ($first_diff_gruop === false) {
          // send message
          $messages[] = $this->generateMessage($path, $diff_group_metadata);
          // reset state
          $diff_group_metadata = new DiffGroupMetadata();
        }

        $diff_group_metadata->old_line_no = $matches["old_line_no"];
        $diff_group_metadata->old_char_no = $matches["old_char_no"];
        $diff_group_metadata->new_line_no = $matches["new_line_no"];
        $diff_group_metadata->new_char_no = $matches["new_char_no"];

        $first_diff_gruop = false;
        continue;
      }

      // old lines
      $match = preg_match(
        '/^-(?<old_line>.*)$/',
        $line,
        $matches
      );

      if ($match) {
        $diff_group_metadata->old_lines[] = $matches["old_line"];
        $diff_group_metadata->done_with_context_processing = true;
        continue;
      }

      // new lines
      $match = preg_match(
        '/^\+(?<new_line>.*)$/',
        $line,
        $matches
      );

      if ($match) {
        $diff_group_metadata->new_lines[] = $matches["new_line"];
        $diff_group_metadata->done_with_context_processing = true;
        continue;
      }

      if ($first_diff_gruop === false &&
          $diff_group_metadata->done_with_context_processing === false) {
        $diff_group_metadata->context_lines += 1;
      }
    }

    if ($first_diff_gruop === false) {
      $messages[] = $this->generateMessage($path, $diff_group_metadata);
    }

    return $messages;
  }
}

final class DiffGroupMetadata {
  public $file_path = "";

  public $old_line_no = 0;
  public $old_char_no = 0;
  public $old_lines = array();

  public $new_line_no = 0;
  public $new_char_no = 0;
  public $new_lines = array();

  public $done_with_context_processing = false;
  public $context_lines = 0;
}
