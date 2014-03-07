#!/bin/bash

# make sure $d is a valid path and source some generic functions
[ "$0" == "${0//\//}" ] && d='.' || d="${0%/*}"
. "$d/bash/inc.sh"

lic_file='/etc/op5license/op5license.xml'

[ -f "$lic_file" ] || \
  dieplug '2' "No op5 license has been installed."
[ -r "$lic_file" ] || \
  dieplug '3' "Unable to read op5 license file ($lic_file)."

depchk op5license-verify

if op5license-verify "$lic_file" &> /dev/null; then
  dieplug '0' 'op5 license file valid.'
else
  dieplug '2' 'op5 license file invalid.'
fi
