#!/usr/bin/env python3

#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
import getopt
import os
import re
import subprocess
import sys


class YBProf:
    file_prefix_ = ""

    total_in_use_count_ = 0
    total_in_use_bytes_ = 0
    total_alloc_count_ = 0
    total_alloc_bytes_ = 0
    records_ = []
    symbols_ = {}

    def __init__(self, output_prefix, pprof_url, seconds):
        self.file_prefix_ = output_prefix
        self.pprof_url_ = pprof_url
        self.seconds_ = seconds

    def parse_header_line(self, line):
        print('header: %s' % line)
        m = re.match(r"heap profile: *(\d+): *(\d+) *\[ *(\d+): *(\d+)\]", line)
        if m:
            self.total_in_use_count_ = int(m.group(1))
            self.total_in_use_bytes_ = int(m.group(2))
            self.total_alloc_count_ = int(m.group(3))
            self.total_alloc_bytes_ = int(m.group(4))
        else:
            print("Unexpected header format: %s" % line)

    def parse_stack_line(self, line):
        # print('body: %s' % line)
        m = re.match(r" *(\d+): *(\d+) *\[ *(\d+): *(\d+)\] \@ (.*)\n", line)
        if m:
            in_use_count = int(m.group(1))
            in_use_bytes = int(m.group(2))
            alloc_count = int(m.group(3))
            alloc_bytes = int(m.group(4))
            stack = m.group(5)
            functions = re.split(" ", stack)

            # Add to a set of function addresses that we will symbolize later.
            for f in functions:
                self.symbols_[f] = ""

            # Remember this record (the call stack and associated counters).
            self.records_.append({"in_use_count": in_use_count,
                                  "in_use_bytes": in_use_bytes,
                                  "alloc_count": alloc_count,
                                  "alloc_bytes": alloc_bytes,
                                  "stack": functions})
        else:
            print("Unexpected format in line: %s" % line)

    def invoke_heap_profile_handler(self):
        heap_profile_url = self.pprof_url_ + "/heap?seconds=" + str(self.seconds_)
        raw_output_file = self.file_prefix_ + ".raw.txt"
        print("Invoking heap profile handler: " + heap_profile_url)
        output_fhd = open(raw_output_file, "w")
        result = subprocess.call(["curl", heap_profile_url], stdout=output_fhd)
        print("Raw output: " + raw_output_file)
        self.parse_heap_file(raw_output_file)

    def symbolize_all(self):
        print("Total Symbols " + str(len(self.symbols_)) + " symbols...")

        # Symbolize the above symbols few at a time.
        chunk_of_symbols = []
        cnt = 0
        for key in self.symbols_.keys():
            chunk_of_symbols.append(key)
            cnt = cnt + 1
            if (len(chunk_of_symbols) == 25):
                self.symbolize(chunk_of_symbols)
                chunk_of_symbols = []
                print("Completed symbolizing %d/%d symbols." % (cnt, len(self.symbols_)))

        if (len(chunk_of_symbols) > 0):
            self.symbolize(chunk_of_symbols)

    def symbolize(self, symbols):
        arg = "+".join(symbols)
        result = subprocess.check_output(
          ["curl",
           "--silent",
           "-d",
           arg,
           self.pprof_url_ + "/symbol"])
        lines = result.decode().split("\n")
        for line in lines:
            # Use whitespace as the delimiter, and produce no more than two parts
            # (i.e. max of 1 split). The symbolized function name may itself contain
            # spaces. A few examples below (the latter two are example with spaces
            # in the name of the function).
            #
            # 0x7fff950fcd23  std::__1::basic_string<>::append()
            # 0x7fff950f943e  operator new()
            # 0x102296ceb     yb::tserver::(anonymous namespace)::SetLastRow()
            parts = line.split(None, 1)
            num_parts = len(parts)
            if num_parts == 0:
                continue
            elif num_parts == 2:
                addr = parts[0]
                symbol = parts[1]
                self.symbols_[addr] = symbol
            else:
                print("Unexpected output line: " + line)

    def print_records(self, sort_metric, filename):
        max_call_stacks = 1000
        idx = 0
        print("Writing output to " + filename)
        fhd = open(filename, "w")
        fhd.write("<html>\n")
        fhd.write("<title>Top Call Stacks By: " + sort_metric + "</title>\n")
        fhd.write("<body style=\"font-family: sans-serif\">\n")
        fhd.write("<b>Top " + str(max_call_stacks) + " Call Stacks By: " + sort_metric + "</b>\n")
        fhd.write("<p>\n")
        fhd.write("<table style=\"border-collapse: collapse\" border=1 cellpadding=5>\n")
        fhd.write("<tr>\n")
        fhd.write("<th>In Use Cnt</th>\n")
        fhd.write("<th>In Use Bytes</th>\n")
        fhd.write("<th>In Use Avg Size</th>\n")
        fhd.write("<th>Alloc Cnt</th>\n")
        fhd.write("<th>Alloc Bytes</th>\n")
        fhd.write("<th>Alloc Avg Size</th>\n")
        fhd.write("<th>Call Stack</th>\n")
        fhd.write("</tr>\n")
        sorted_records = sorted(self.records_, key=lambda k: k[sort_metric], reverse=True)
        for record in sorted_records:
            if (idx == max_call_stacks):
                break
            idx = idx + 1
            functions = []
            for addr in record.get("stack"):
                fname = self.symbols_[addr]
                # If the symbolization didn't happen for some reason,
                # print the address itself for the function name.
                if fname == "":
                    fname = addr
                functions.append(fname)

            fhd.write("<tr>\n")

            in_use_count = record.get("in_use_count")
            in_use_bytes = record.get("in_use_bytes")
            in_use_avg = 0 if in_use_count == 0 else in_use_bytes * 1.0 / in_use_count
            alloc_count = record.get("alloc_count")
            alloc_bytes = record.get("alloc_bytes")
            alloc_avg = 0 if alloc_count == 0 else alloc_bytes * 1.0 / alloc_count
            stack = "\n".join(functions)

            fhd.write(("<td>%d</td><td>%d</td><td>%.2f</td><td>%d</td>" +
                       "<td>%d</td><td>%.2f</td><td><pre>%s</pre></td>") %
                      (in_use_count, in_use_bytes, in_use_avg,
                       alloc_count, alloc_bytes, alloc_avg,
                       stack))

            fhd.write("</tr>\n")
        fhd.write("</table>")
        fhd.write("</body>")
        fhd.write("</html>\n")
        fhd.close()

    def parse_heap_file(self, filename):
        line_num = 0
        with open(filename) as f:
            for line in f:
                line_num = line_num + 1
                if line_num == 1:
                    self.parse_header_line(line)
                elif line == "\n":
                    continue
                elif line == "MAPPED_LIBRARIES:\n":
                    print("End of stacks...")
                    break
                else:
                    self.parse_stack_line(line)
        self.symbolize_all()
        self.print_records("in_use_bytes", self.file_prefix_ + ".in_use_bytes.html")
        self.print_records("alloc_bytes", self.file_prefix_ + ".alloc_bytes.html")


def print_usage():
    print("Usage:")
    print('yb-prof.py --profile_url=<url> --output_file_prefix=<file_prefix> ' +
          '--seconds=<time_in_seconds>')
    print("**********************")


def main(argv):
    profile_url = ''
    output_prefix = ''
    seconds = '20'
    try:
        opts, args = getopt.getopt(argv, "h:", ["profile_url=", "output_file_prefix=", "seconds="])
    except getopt.GetoptError:
        print("Incorrect getopt options")
        print_usage()
        sys.exit(1)
    for opt, arg in opts:
        if opt == '-h':
            print_usage()
            sys.exit(1)
        elif opt in ("--profile_url"):
            profile_url = arg
        elif opt in ("--output_file_prefix"):
            output_file_prefix = arg
        elif opt in ("--seconds"):
            seconds = int(arg)
        else:
            assert False, "** Unhandled option. **"
    if ((profile_url == '') or (output_file_prefix == '')):
        print_usage()
        sys.exit(1)
    print("Profile Base URL:" + profile_url)
    print("Output File Prefix:" + output_file_prefix)
    heap_prof = YBProf(output_file_prefix, profile_url, seconds)
    heap_prof.invoke_heap_profile_handler()


if __name__ == "__main__":
    main(sys.argv[1:])
