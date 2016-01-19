#!/bin/bash

if [ ! -f config.mak ]; then
	echo "run ./configure && make first. See ./configure -h"
	exit
fi
source config.mak
source scripts/functions.bash

one_kernel="$1"
[ "$one_kernel" ] && one_kernel_base=$(basename $one_kernel)
one_testname="$2"
if [ -n "$one_kernel" ] && [ ! -f $one_kernel ]; then
	echo "$one_kernel doesn't exist"
	exit 1
elif [ -n "$one_kernel" ] && [ -z "$one_testname" ]; then
	one_testname="${one_kernel_base%.*}"
fi

unittests=$TEST_DIR/unittests.cfg
mkdir -p tests

escape ()
{
	for arg in "${@}"; do
		printf "%q " "$arg"; # XXX: trailing whitespace
	done
}

temp_file ()
{
	local var="$1"
	local file="$2"

	echo "$var=\`mktemp\`"
	echo "cleanup=\"\$$var \$cleanup\""
	echo "base64 -d << 'BIN_EOF' | zcat > \$$var || exit 1"

	gzip - < $file | base64

	echo "BIN_EOF"
	echo "chmod +x \$$var"
}

function mkstandalone()
{
	local testname="$1"
	local args=( $(escape "${@}") )

	if [ -z "$testname" ]; then
		return 1
	fi

	if [ -n "$one_testname" ] && [ "$testname" != "$one_testname" ]; then
		return 1
	fi

	standalone=tests/$testname

	exec {tmpfd}<&1
	exec > $standalone

	echo "#!/bin/bash"
	grep '^ARCH=' config.mak

	if [ ! -f $kernel ]; then
		echo 'echo "skip '"$testname"' (test kernel not present)"'
		echo 'exit 1'
	else
	# XXX: bad indentation

	echo "trap 'rm -f \$cleanup' EXIT"

	temp_file bin "$kernel"
	args[3]='$bin'

	temp_file RUNTIME_arch_run "$TEST_DIR/run"

	cat scripts/runtime.bash

	echo "run ${args[@]}"
	echo "exit 0"
	fi

	exec 1<&$tmpfd {tmpfd}<&-
	chmod +x $standalone

	return 0
}

trap 'rm -f $cfg; exit 1' HUP INT TERM
trap 'rm -f $cfg' EXIT
cfg=$(mktemp)

if [ -n "$one_testname" ]; then
	if grep -q "\[$one_testname\]" $unittests; then
		sed -n "/\\[$one_testname\\]/,/^\\[/p" $unittests \
			| awk '!/^\[/ || NR == 1' > $cfg
	else
		echo "[$one_testname]" > $cfg
		echo "file = $one_kernel_base" >> $cfg
	fi
else
	cp -f $unittests $cfg
fi

for_each_unittest $cfg mkstandalone
