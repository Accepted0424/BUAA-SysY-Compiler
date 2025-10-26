#!/bin/bash

if [ "$1" == "release" ]; then
  if [ "$2" == "hw2" ]; then
    zip -r hw2.zip src CMakeLists.txt config.json
  fi

  if [ "$2" == "hw3" ]; then
    zip -r hw3.zip src CMakeLists.txt config.json myCompiler.sh test
  fi
fi