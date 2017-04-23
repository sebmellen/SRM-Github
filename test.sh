#!/bin/sh
# test script for srm

set -e # exit if any commands fail

make
test/fill_test

SRM="src/srm -vvvvvvv"

# test different file types

testremove()
{
    $SRM -rf "$1"
    if [ -e "$1" ] ; then
	echo could not remove $1
	exit 1
    fi
}

# symlink
echo "testing symlinks..."
rm -f test.symlink
echo "TEST" > test.file
if [ ! -f test.file ] ; then
    echo could not create test file
    exit 1
fi

ln -s test.file test.symlink
if [ ! -L test.symlink ] ; then
    echo could not create symlink
    exit 1
fi
testremove test.symlink

I=`whoami`
if [ "$I" != root ] ; then
    echo "testing symlink to root's files..."
    ln -s /etc/fstab test.symlink
    if [ ! -L test.symlink ] ; then
	echo could not create symlink
	exit 1
    fi
    testremove test.symlink
else
    echo "can not test symlink to root's files, test.sh is run by root."
fi

# permissions
echo
echo "testing file permissions..."
echo "TEST" > test.file2
chmod 000 test.file2
testremove test.file2

# recursing into directories
echo
echo "testing recursing into directories..."
rm -rf test.dir
mkdir test.dir
if [ ! -d test.dir ] ; then
    echo could not mkdir test.dir
    exit 1
fi

echo "TEST2" > test.dir/file2
cd test.dir
ln -s file2 link2
if [ ! -L link2 ] ; then
    echo link2
    exit 1
fi
cd ..

testremove test.dir

# device nodes
echo
if [ "$I" = root ] ; then
    echo "testing device nodes..."

    mkdir test.dir2
    echo "TEST" > test.dir2/test.file
    chmod 000 test.dir2/test.file
    chmod 000 test.dir2
    testremove test.dir2

    mknod test.char c 1 1
    if [ ! -c test.char ] ; then
	echo could not mknod test.char
	exit 1
    fi
    testremove test.char

    mknod test.block b 1 1
    if [ ! -b test.block ] ; then
	echo could not mknod test.block
	exit 1
    fi
    testremove test.block

else
    echo "not running test.sh as root, not testing device nodes"
fi

# FIFO special file
echo
echo "testing FIFO..."
mkfifo test.fifo
if [ ! -p test.fifo ] ; then
    echo could not create fifo
    exit 1
fi
testremove test.fifo

# hard links
echo
echo "testing hard links..."
echo "the hard link" > test.file
cp -f test.file test2.file
ln test.file test3.file
if ! cmp test.file test3.file ; then
    echo "compare of test.file and test3.file failed on hard link test"
    exit 1
fi
testremove test3.file
if ! cmp test.file test2.file ; then
    echo "compare of test.file and test2.file failed on hard link test"
    exit 1
fi
testremove test2.file
if [ -f test2.file ] ; then
    echo "could not remove test2.file on hard link test"
    exit 1
fi
if [ -f test3.file ] ; then
    echo "could not remove test3.file on hard link test"
    exit 1
fi

testremove test.file
if [ -f test.file ] ; then
    echo could not remove test file
    exit 1
fi

# test directory symlink
echo
echo "testing directory symlinks..."
mkdir test.dir
cd test.dir
echo test > test.file
ln -s test.file test.link
mkdir -p /tmp/doj
echo test2 > /tmp/doj/test.file2
ln -s /tmp/doj doj
cd ..
testremove test.dir
if [ ! -f /tmp/doj/test.file2 ] ; then
    echo srm deleted /tmp/doj/test.file2 instead of directory symlink
    exit 1
fi

# test return code if remove fails
if [ "$USER" = doj -a "$HOST" = cubicle.cubic.org ] ; then
    cp -f /etc/fstab "/tmp/$USER.fstab"
    set +e
    if $SRM -f /etc/fstab ; then
	echo "removing /etc/fstab as user should not work"
	exit 1
    fi
    set -e
fi

# test extended attributes
if [ 1 = 2 ] ; then
echo
echo "test extended attributes..."
DIR=test.test
mkdir -p $DIR
chmod +t $DIR
FN=$DIR/test.atr
echo "TEST" > $FN
OS=$(uname -s)
if [ "$OS" = Linux ] ; then
    setfattr -n user.a1 -v "The value of extended attribute number 1" $FN
    setfattr -n user.num2 -v "A second attribute." $FN
elif [ "$OS" = FreeBSD ] ; then
    setextattr user a1 "The value of extended attribute number 1" $FN
    setextattr user num2 "A second attribute." $FN
elif [ "$OS" = Darwin ] ; then
    xattr -w a1 "The value of extended attribute number 1" $FN
    xattr -w num2 "A second attribute." $FN
fi
$SRM $FN
rmdir $DIR
fi

# test file sizes

BS=123
SRC=/dev/zero # /dev/urandom

if [ ! -c $SRC ] ; then
    echo $SRC not present or no char device
    exit 1
fi

testsrm()
{
    if [ ! -f $F ] ; then
	echo failed to create $F
	exit 1
    fi
    if ! $SRM $F ; then
	echo failed to secure remove $F
	exit 1
    fi
    if [ -f $F ] ; then
	echo srm was not able to remove $F
	exit 1
    fi
}

F="0.tst"
touch $F
testsrm

# test until ~5GiB
for i in 1 22 333 4444 55555 666666 7777777 44444444
do
    echo
    echo
    echo "test file of $(($i * $BS / 1024)) KB..."
    F="$i.tst"
    dd if=$SRC of=$F bs=$BS count=$i 2> /dev/null
    testsrm
done

echo "all tests successful."
exit 0
