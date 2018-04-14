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

Example
=======
Check out `util/uaparser.c` for a short example program which uses a compiled-in `regexes.yaml`.

API
===
There are two types of structs to work with: `uap_parser` and `uap_useragent_info`.

Begin by creating a new `uap_parser` and load [regexes.yaml](https://github.com/ua-parser/uap-core/blob/master/regexes.yaml)
with either `uap_parser_read_buffer()` or `uap_parser_read_file()`.
```C
struct uap_parser *ua_parser = uap_parser_create();
FILE *fd = fopen("uap-core/regexes.yaml", "r");
uap_parser_read_file(uap_parser, fd);
fclose(fd);
```

Then parse user agent strings with `uap_parser_parse_string()`
```C
struct uap_useragent_info *ua_info = uap_useragent_info_create();

if (uap_parser_parse_string(ua_parser, ua_info, useragent_string)) {
    printf("user_agent.family\t%s\n",  ua_info->user_agent.family);
    printf("user_agent.major\t%s\n",   ua_info->user_agent.major);
    printf("user_agent.minor\t%s\n",   ua_info->user_agent.minor);
    printf("user_agent.patch\t%s\n",   ua_info->user_agent.patch);

    printf("os.family\t%s\n",          ua_info->os.family);
    printf("os.major\t%s\n",           ua_info->os.major);
    printf("os.minor\t%s\n",           ua_info->os.minor);
    printf("os.patch\t%s\n",           ua_info->os.patch);
    printf("os.patchMinor\t%s\n",      ua_info->os.patch);

    printf("device.family\t%s\n",      ua_info->device.family);
    printf("device.brand\t%s\n",       ua_info->device.brand);
    printf("device.model\t%s\n",       ua_info->device.model);
}

uap_useragent_info_destroy(info);
info = NULL;
```

Then clean up the parser when you're all finished.
```C
uap_parser_destroy(ua_parser);
ua_parser = NULL;
```

This library makes an effort to de-dupe repeated strings within `regexes.yaml` to minimize the runtime
memory footprint, but a fully initialized `uap_parser` still consumes a not insignificant amount of memory.
For this reason, when using this library in a multi-threaded capacity, it is advisable to initialize and
use a single `uap_parser` instance across multiple threads. There are no locking mechanisms, `uap_parser`
simply serves the role of a read-only database during calls to `uap_parser_parse_string()`.
