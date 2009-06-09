#!/bin/sh

if [ -f Makefile ]; then
  make
  exit $?
else
  echo "No Makefile found, bailing out"
  exit 1
fi
		
