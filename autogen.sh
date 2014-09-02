#!/bin/bash

autoreconf -si
./configure --enable-silent-rules "$@"
