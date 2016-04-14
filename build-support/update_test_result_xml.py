#!/usr/bin/env python

import logging
import argparse
import os
import sys
import datetime

def main():
  logging.basicConfig(
    level=logging.INFO,
    format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

  parser = argparse.ArgumentParser(
      usage="usage: %(prog)s <options>")

  parser.add_argument(
    "--test-result-xml",
    help="Test result XML file to update",
    type=unicode,
    dest="test_result_xml_path",
    metavar="TEST_RESULT_XML_PATH",
    required=True)

  args = parser.parse_args()
  return 0

if __name__ == "__main__":
  sys.exit(main())


