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
##     Build Script
##
########################################################################

import os

from waflib import Configure, Options, Context
Configure.autoconfig = True

LIBCAPI = os.path.join("libs", "capi")
LIBCAPI_SCRIPTS = os.path.join(LIBCAPI, "scripts")

waf_libs = [
    os.path.join("libs", "argconfig"),
    LIBCAPI,
]

def options(opt):
    if not os.path.exists(LIBCAPI_SCRIPTS):
        opt.fatal("The scripts directory in libcapi is missing. You man need to "
                  "run 'git submodule update --init'")

    opt.load("compiler_c gnu_dirs")
    opt.recurse(waf_libs)

    gr = opt.library_group
    gr.add_option("-H", "--hardware", action="store_true",
                  help="compile for hardware")


def configure(conf):
    conf.load("compiler_c gnu_dirs")

    conf.load("make version", tooldir=LIBCAPI_SCRIPTS)

    conf.env.append_unique("DEFINES", ["_GNU_SOURCE"])
    conf.env.append_unique("INCLUDES", [os.path.join(l, "inc") for l in waf_libs])
    conf.env.append_unique("CFLAGS", ["-std=gnu99", "-O2", "-Wall",
                                      "-Werror", "-g"])

    conf.check_cc(fragment="int main() { return 0; }\n",
                  msg="Checking for working compiler")
    conf.find_program("make", var='MAKE')

    sim = not Options.options.hardware
    top_dir = conf.path.abspath()
    conf.msg("Setting compile mode to", "Simulation" if sim else "Hardware")
    conf.env.PSLSE_DIR = os.path.join(top_dir, "libs", "pslse",
                                      "pslse" if sim else "libcxl")
    if Options.options.pslse_dir:
        conf.env.PSLSE_DIR =  Options.options.pslse_dir
    conf.msg("Setting PSLSE directory to", conf.env.PSLSE_DIR)

    conf.recurse(waf_libs)

    conf.env.LIBCXLFLAGS = ("-DPAGED_RANDOMIZER=0 -g -I %s" %
                            os.path.join(top_dir, LIBCAPI, "inc"))

def build(bld):
    bld.load("version", tooldir=LIBCAPI_SCRIPTS)

    bld.recurse(waf_libs)

    bld.make_stlib(target="cxl",
                   make_dir=bld.env.PSLSE_DIR,
                   make_env={"LIBCFLAGS": bld.env.LIBCXLFLAGS})

    srcs = bld.path.ant_glob("src/*.c", excl=["src/*test.c",
                                              "src/textswap.c",
                                              "src/gen_haystack.c"])
    bld.objects(source=srcs,
                target="build_objs",
                use="argconfig capi cxl")

    bld.program(source="src/textswap.c",
                target="textswap",
                use="build_objs")

    bld.program(source="src/unittest.c",
                target="unittest",
                use="build_objs")

    bld.program(source="src/lfsrtest.c",
                target="lfsrtest",
                use="build_objs")

    bld.program(source="src/searchtest.c",
                target="searchtest",
                use="build_objs")

    bld.program(source="src/gen_haystack.c",
                target="gen_haystack",
                use="argconfig")
