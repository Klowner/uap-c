uap-c [![Build Status](https://travis-ci.org/Klowner/uap-c.svg?branch=master)](https://travis-ci.org/Klowner/uap-c)
=====

A C99 implementation of the parser described by the [ua-parser/uap-core specification](https://github.com/ua-parser/uap-core/blob/master/docs/specification.md).

The most recent version of library is available at https://github.com/Klowner/uap-c

Build Dependencies
============
 - libyaml
 - libpcre3

Runtime Dependencies
====================
When built as a library, the `regexes.yaml` from [ua-parser/uap-core](https://github.com/ua-parser/uap-core/) is required at run time.
To build the command-line tool (`uaparser`), `regexes.yaml` is compiled into the binary, so the `uap-core` repository must
be present in a sibling directory during build time.

