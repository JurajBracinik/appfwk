#!/usr/bin/env bash

# Really ugly and temporary glue to run moo code generator.
# This will simplify and move into CMake.

# For current best ideas for CMake integration see:
# https://brettviren.github.io/moo/buildsys.html#cmake

# For guidance on how to write schema see:
# https://brettviren.github.io/moo/dunedaq-appfwk-schema.html

# For guidance on how to make schema objects see:
# https://brettviren.github.io/dune-daq-repl/ddcmd.html

# For guidance on how to deliver objects to daq_application see:
# https://brettviren.github.io/dune-daq-repl/ddrepl.html

mydir=$(dirname $(realpath $BASH_SOURCE))
srcdir=$(dirname $mydir)

# Wrap up the render command.  This bakes in a mapping to file name
# which would be better somehow captured by the schema itself.
render () {
    local name="$1" ; shift
    local What="$1" ; shift

    local name_lc=$( echo "$name" | tr '[:upper:]' '[:lower:]' )
    local outdir="${1:-$srcdir/include/appfwk/${name}}"
    local what="$(echo $What | tr '[:upper:]' '[:lower:]')"
    local tmpl="appfwk-${what}.hpp.j2"
    local outhpp="$outdir/${What}.hpp"
    mkdir -p $outdir
    set -x
    moo -g '/lang:ocpp.jsonnet' \
        -M $mydir -T $mydir \
        -A path="dunedaq.appfwk.${name_lc}" \
        -A ctxpath="dunedaq" \
        -A os="appfwk-${name}-schema.jsonnet" \
        render appfwk-model.jsonnet $tmpl \
        > $outhpp || exit -1
    set +x
    echo $outhpp
}


render cmd Structs
render cmd Nljs

# JCF, Nov-19-2020

# The production of Structs.hpp and Nljs.hpp for the fake data modules
# here is commented out since this is now handled automatically by the
# build system (see daq-cmake Issue #1 for details).

#render FakeDataProducerDAQModule Structs $srcdir/test/src/appfwk/fakedataproducerdaqmodule
#render FakeDataProducerDAQModule Nljs    $srcdir/test/src/appfwk/fakedataproducerdaqmodule

#render FakeDataConsumerDAQModule Structs $srcdir/test/src/appfwk/fakedataconsumerdaqmodule
#render FakeDataConsumerDAQModule Nljs    $srcdir/test/src/appfwk/fakedataconsumerdaqmodule
