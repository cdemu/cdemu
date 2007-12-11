#!/bin/sh

echo "Generating files for cdemu-client:"

echo "Running intltoolize..."
intltoolize --copy --force || exit 1;
echo "Running aclocal..."
aclocal $ACLOCAL_FLAGS || exit 1;
echo "Running automake..."
automake --copy --add-missing || exit 1;
echo "Running autoconf..."
autoconf || exit 1;

echo "Done!"
