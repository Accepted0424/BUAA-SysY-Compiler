#!/bin/bash

if [ "$1" == "release" ]; then
  if [ "$2" == "hw2" ]; then
    zip -r hw2.zip src CMakeLists.txt config.json
  fi
fi