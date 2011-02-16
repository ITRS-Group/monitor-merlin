#!/bin/sh

DEF_VER=v1.1.0
revision= patches= version= ver= beta= dirty=

# First see if we can use git-describe, then check for a version file
# (included in release tarballs). Fall back to default if neither works
ver=`git describe --abbrev=12 HEAD 2>/dev/null`
if test $? -eq 0; then
	tag=`git describe --abbrev=0 HEAD 2>/dev/null`
	case "$ver" in
	v[0-9]*)
		git update-index -q --refresh >/dev/null 2>&1
		test -z "`git diff-index --name-only HEAD --`" ||
		dirty="-dirty"
	esac
	if test "$ver" != "$tag"; then
		beta=`expr "$ver" : .*[0-9]-'\(beta[0-9]*\)'`
		revision=`expr "$ver" : '.*\(-g[a-f0-9]*\)$'`
		version=`expr "$ver" : '\(.*\)-g[a-f0-9*]'`
		patches=p`expr "$ver" : $tag-'\(.*\)'$revision`
		ver=`expr "$ver" : v*'\(.*\)'`$dirty
	fi
	version="$tag$patches$revision$dirty"
elif test -f version; then
	ver=`cat version` || ver="$DEF_VER"
else
	version="$DEF_VER"
fi
cat << EOF > version.c
#include "shared.h"
const char *merlin_version = "$version";
EOF
exit 0
echo "beta=$beta"
echo "patches=$patches"
echo "version=$version"
echo "revision=$revision"
echo "tag=$tag"
echo "ver=$ver"
