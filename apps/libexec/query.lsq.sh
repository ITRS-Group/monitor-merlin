#!/bin/bash

[ -n "$LS_SOCK" ] || LS_SOCK='/opt/monitor/var/rw/live'

if [ -n "$1" ]; then
  printf '%s\n' "$1" | unixcat "$LS_SOCK"
else
  unixcat "$LS_SOCK"
fi
