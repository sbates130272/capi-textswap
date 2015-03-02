#!/usr/bin/env python
########################################################################
##
## Copyright 2015 PMC-Sierra, Inc.
##
## Licensed under the Apache License, Version 2.0 (the "License"); you
## may not use this file except in compliance with the License. You may
## obtain a copy of the License at
## http://www.apache.org/licenses/LICENSE-2.0 Unless required by
## applicable law or agreed to in writing, software distributed under the
## License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
## CONDITIONS OF ANY KIND, either express or implied. See the License for
## the specific language governing permissions and limitations under the
## License.
##
########################################################################

########################################################################
##
##   Author: Logan Gunthorpe
##
##   Description:
##     Script to obtain a graph of CPU and RAM usage numbers for a 
##     single process (specified by -C).
##
########################################################################

from __future__ import print_function
from __future__ import unicode_literals

import sys
import subprocess as sp
import time

def get_perfdata(options):
    try:
        data = sp.check_output(["ps", "-C", options.command, "-o" "%cpu=,%mem="])
        data = tuple(float(x) for x in data.split())
    except sp.CalledProcessError:
        if (options.skip):
            data=None
        else:
            data = 0.,0.

    return data


if __name__=="__main__":
    import optparse

    parser = optparse.OptionParser()
    parser.add_option("-C", "--command", action="store", type="string",
                      help="The command to look for in the ps log.", default="textswap")
    parser.add_option("-t", "--time", action="store", type="int",
                      help="Time to run for in seconds (-1 to run forever)", default=-1)
    parser.add_option("-w", "--wait", action="store", type="int",
                      help="Wait time in ms between calls to ps.", default=100)
    parser.add_option("-s", "--skip", action="store_true",
                      help="Only output data when command is running.")
    (options, args) = parser.parse_args()

    try:
        start_time = time.time()
        end_time = start_time + options.time
        print("#%7s   %3s   %3s" % ("TIME", "CPU", "MEM"))
        while options.time < 0 or time.time() < end_time:
            t = time.time()-start_time
            data = get_perfdata(options)
            if data:
                print("%8.1f   %-3.1f   %3.1f" % ((t,) + data))
                sys.stdout.flush()
            time.sleep(options.wait / 1000.)

    except KeyboardInterrupt:
        print
