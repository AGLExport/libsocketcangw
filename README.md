# The socket can gw helper library

## Overview

The libsocketcangw is a helper library for the socket can gw feature.

This library focuses to provide configuration feature for can message gateway.

## Configuration checker

The checker validates the configuration format and reports duplicate JSON keys,
including keys in nested objects. It prints diagnostics to stderr and exits with
0 for a valid configuration, 1 for a validation or file error, and 2 for invalid
arguments. Use `--help` to display usage. No CAN devices or root privileges are
needed.

```sh
cangw-config-check gw-rule.json
```

## Maintainer

The libsocketcangw is maintained by AGL community.

## Reference

### Automotive Grade Linux

https://www.automotivelinux.org/

### Automotive Grade Linux Instrument Cluster Expert Group.

https://confluence.automotivelinux.org/display/IC/Instrument+Cluster+Home

### Automotive Grade LinuxInstrument Cluster Expert Group github.

https://github.com/agl-ic-eg

