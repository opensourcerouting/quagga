#!/bin/sh


if [ -d .git ] ; then
# generate GNU/Debian format ChangeLog from git log

    rm -f ChangeLog

    if which git2cl >/dev/null ; then
	git-log --pretty --numstat --summary | git2cl >> ChangeLog
    else
	git-log --pretty=short >> ChangeLog
    fi

# append repository reference

    url=` git repo-config --get remote.origin.url`
    test "x$url" = "x" && url=`pwd`

    branch=`git-branch --no-color | sed '/^\* /!d; s/^\* //'`
    test "x$branch" = "x" && branch=master

    sha=`git log --pretty=oneline --no-color -n 1 | cut -c-8`
    test "x$sha" = "x" && sha=00000000

    echo "$url#$branch-$sha" >> ChangeLog

fi

rm -rf config
rm -f aclocal.m4 config.guess config.statusconfig.sub configure INSTALL

autoreconf --force --install

rm -f config.sub config.guess
ln -s /usr/share/misc/config.sub .
ln -s /usr/share/misc/config.guess .
