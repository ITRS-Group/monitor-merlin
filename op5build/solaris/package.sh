#!/bin/bash

eval `cat pkginfo | grep NAME`
product=$NAME
version=default
user=default
tmpdir=/tmp/build
arch=`uname -p`
gsed -i "s/^ARCH=.*/ARCH=$arch/" pkginfo

rm -rf temp prototype instantclient_11_2

while getopts "hv:" OPTION; do
  case $OPTION in
	h)
	echo " -v <version>"
	exit 1
	;;
	v)
	version=$OPTARG
	;;
	*)
	echo "BadArgs"
	exit 1
 esac
done

if test "$version" = "default"; then
	echo "Must supply version. Try the -v argument"
	exit 1
fi

echo "Building $product $version"

####
if [ -f build ]; then
	/bin/bash build;
fi
####

for i in pkginfo checkinstall preinstall postinstall; do
	test -f $i || continue
	echo "i $i" >> prototype
done

cd temp
pkgproto * >> ../prototype

sed -e "s/@@VERSION@@/$version/g" ../pkginfo > ../pkginfo1 && mv ../pkginfo1 ../pkginfo
mkdir -p $tmpdir/$product

pkgmk -o -r ./ -d $tmpdir/$product -f ../prototype
cd ..

eval `cat pkginfo | grep PKG`
eval `cat pkginfo | grep ARCH`

pkgtrans -s $tmpdir/$product `pwd`/$PKG-$version-$ARCH.pkg $PKG
echo "Package ready: $PKG-$version-$ARCH.pkg"

echo "Performing cleanup..."
rm -rf $working_dir/temp
rm -rf $tmpdir
echo "Done"
