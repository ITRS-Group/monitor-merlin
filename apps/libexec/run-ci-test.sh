#!/bin/bash

ret=0
ok=0
failed=0
total=0

function increment
{
	if [ $1 -gt 0 ]; then
		ret=$((ret+$1))
		failed=$((failed+1))
		echo "========================================"
		echo "Test Failed..."
		echo ""
	else
		ok=$((ok+1))
		echo "========================================"
		echo "Test Success..."
		echo ""
	fi
	total=$((total+1))
}

su - monitor -c "mon test dist --batch --basepath=/tmp/logs"
increment $?

cp -r /tmp/logs/* /mnt/logs/ || :
exit $ret
