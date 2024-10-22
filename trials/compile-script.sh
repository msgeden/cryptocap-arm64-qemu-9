#!/bin/bash

# Check if a file name is provided as an argument
if [ -z "$1" ]; then
  echo "Usage: $0 <file_name>"
  exit 1
fi

# Get the file name from the first argument
FILE_NAME=$1

# Step 1: Compile the C file with aarch64-linux-gnu-gcc
aarch64-linux-gnu-gcc ${FILE_NAME}.c -g   -o ${FILE_NAME} -static
if [ $? -ne 0 ]; then
  echo "GCC compilation failed"
  exit 1
fi

# Step 2: Generate the object dump of the compiled file
aarch64-linux-gnu-objdump -d ${FILE_NAME} > ${FILE_NAME}.objdmp
if [ $? -ne 0 ]; then
  echo "Object dump failed"
  exit 1
fi

# Step 3: Change directory up one level
cd ..
if [ $? -ne 0 ]; then
  echo "Directory change failed"
  exit 1
fi

# Step 4: Find all files and create an initramfs
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initramfs.cpio.gz
if [ $? -ne 0 ]; then
  echo "Initramfs creation failed"
  exit 1
fi

echo "Script completed successfully!"

