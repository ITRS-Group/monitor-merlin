#!/bin/sh

DEF_VER=v0.6.7-beta
revision= patches= version= ver= beta= dirty=

# First see if there is a version file (included in release tarballs),
# then try git-describe, then default.
if test -f version; then
	ver=$(cat version) || ver="$DEF_VER"
elif test -d .git -o -f .git; then
	ver=$(git describe --abbrev=12 HEAD 2>/dev/null)
	tag=$(git describe --abbrev=0 HEAD 2>/dev/null)
	case "$ver" in
	v[0-9]*)
		git update-index -q --refresh
		test -z "$(git diff-index --name-only HEAD --)" ||
		dirty="-dirty"
	esac
	if ! test "$ver" = "$tag"; then
		beta=$(expr "$ver" : .*[0-9]-'\(beta[0-9]*\)')
		revision=$(expr "$ver" : '.*\(-g[a-f0-9]*\)$')
		version=$(expr "$ver" : '\(.*\)-g[a-f0-9*]')
		patches=p$(expr "$ver" : $tag-'\(.*\)'$revision)
		ver=$(expr "$ver" : v*'\(.*\)')$dirty
	fi
	version="$tag$patches$revision$dirty"
else
	version="$DEF_VER"
fi

echo "#include \"shared.h\""
echo "const char *merlin_version = \"$version\";";
exit 0
echo "beta=$beta"
echo "patches=$patches"
echo "version=$version"
echo "revision=$revision"
echo "tag=$tag"
echo "ver=$ver"
