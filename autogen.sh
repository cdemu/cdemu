#!/bin/sh

echo "Generating files for image-analyzer:"

echo "Running aclocal..."
aclocal $ACLOCAL_FLAGS || exit 1;
echo "Running autoheader..."
autoheader || exit 1;
echo "Running automake..."
automake --copy --add-missing || exit 1;
echo "Running autoconf..."
autoconf || exit 1;

echo "Done!"
