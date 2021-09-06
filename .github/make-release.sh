#!/bin/bash

# Check for OBS_TOKEN
if [ -z "${OBS_PROJECT}" ] ; then
    echo "Enviorment variable OBS_PROJECT must be set"
    exit 1
fi
if [ -z "${OBS_PACKAGE}" ] ; then
    echo "Enviorment variable OBS_PACKAGE must be set"
    exit 1
fi
if [ -z "${MERLIN_VERSION}" ] ; then
    echo "Enviorment variable MERLIN_VERSION must be set"
    exit 1
fi

# TODO: automate getting platforms from OBS
PLATFORMS=(CentOS_8_Stream CentOS_7)
EXPECTED_PACKAGES=${#PLATFORMS[@]}

# Trigger the OBS build
osc api -X POST "/source/$OBS_PROJECT/$OBS_PACKAGE?cmd=runservice"

echo "Waiting for builds to finish"

# list of possible OBS errors
errors=(broken unresolvable failed)

# loop until we built all packages or we get an error or max timeout (2hours)
end=$((SECONDS+7200))
failed=true
while [ $SECONDS -lt $end ]
do
    sleep 60
    obs_status=$(osc r $OBS_PROJECT $OBS_PACKAGE)
    published_count=$(echo $obs_status | grep -o "succeeded" | wc -l)
    if [ $published_count -ge $EXPECTED_PACKAGES ]
    then
        failed=false
        break
    fi
    # check for possible errors
    for error in ${errors[*]}
    do
        error_count=$(echo $obs_status | grep -o "$error" | wc -l)
        if [ $error_count -ge 1 ]
        then
            echo "OBS build failed"
            echo $obs_status
            exit 1;
        fi
    done
done

# check if everything worked OK
if $failed; then
    echo "Timeout"
    exit 1
fi

# get rpms for each release, tar them and cleanup
mkdir -p release_archives
for platform in ${PLATFORMS[*]}
do
    echo $platform
    osc getbinaries $OBS_PROJECT $OBS_PACKAGE $platform x86_64 -d ./$platform
    # get rid of metadata files
    rm -f ./$platform/_*
    # Get slim packages and delete them
    tar cfJ release_archives/merlin-slim-$MERLIN_VERSION-$platform.tar.xz ./$platform/*slim*
    rm ./$platform/*slim*
    # get the normal packages
    tar cfJ release_archives/merlin-$MERLIN_VERSION-$platform.tar.xz ./$platform/*
    # clean up
    rm -rf ./$platform/
done
