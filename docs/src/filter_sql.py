#!/usr/bin/env python3
import sys
import os

def process_line(line, skip_flag):
    """
    Processes the line by replacing schema names and managing the skip flag for 'RETURNS'.
    :param line: The line to process.
    :param skip_flag: Boolean flag to indicate if lines should be skipped.
    :return: Updated skip_flag and processed line (or empty string if skipped).
    """
    # If the line starts with "RETURNS", set the skip_flag and ignore this and following lines
    if line.strip().startswith("RETURNS"):
        return True, ";"  # Stop after 'RETURNS' and output a semicolon

    if line.strip().startswith("DROP FUNCTION"):
        return False, ""

    # If it's a single-line comment that doesn't contain Doxygen-style tags, skip it
    if line.strip().startswith("--"):
        return False, ""

    # Replace schema names
    line = line.replace('__API_SCHEMA__', 'documentdb_api').replace('__CORE_SCHEMA__', 'documentdb_core')
    line = line.replace('__API_SCHEMA_V2__', 'documentdb_api').replace('__CORE_SCHEMA__', 'documentdb_core')
    line = line.replace('helio_api', 'documentdb_api').replace('helio_core', 'documentdb_core')
    # Remove 'CREATE OR REPLACE FUNCTION'
    line = line.replace('CREATE OR REPLACE FUNCTION', '')
    line = line.replace('CREATE OR REPLACE PROCEDURE', '')

    # Ignore multi-line comments that start with /* and end with */
    if line.strip().startswith("/*") and not line.strip().startswith("/**"):
        skip_flag = True  # Start skipping lines in multi-line comment

    # If the line contains a Doxygen-style comment, allow it through
    if line.strip().startswith("/**"):
        return False, line  # Keep the Doxygen comment

    if skip_flag:
        # If it's the end of a multi-line comment, stop skipping
        if "*/" in line:
            skip_flag = False
        return skip_flag, ""  # Skip the content inside the multi-line comment

    return skip_flag, line

def process_file(fh):
    skip_flag = False  # Flag to control skipping lines in multi-line comments
    for line in fh:
        # Process the line, replace schemas, and check for skipping
        skip_flag, processed_line = process_line(line, skip_flag)
        if processed_line:
            sys.stdout.write(processed_line)

def main(file):
    if file == '-':
        process_file(sys.stdin)
    else:
        try:
            with open(file, 'r') as fh:
                process_file(fh)
        except OSError as err:
            print(f"ERROR: can't open '{err.filename}' for read: {err.strerror} (errno={err.errno})", file=sys.stderr)

if __name__ == '__main__':
    if len(sys.argv) >= 2:
        main(sys.argv[1])
    else:
        pname = os.path.basename(__file__)
        print(f'{pname}\n\nUSAGE: {pname} <file>.sql', file=sys.stderr)
