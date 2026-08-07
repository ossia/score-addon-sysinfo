#!/bin/bash
rm -rf release
mkdir -p release

cp -rf SysInfo 3rdparty *.{hpp,cpp,txt,cmake,json} LICENSE release/

mv release score-addon-sysinfo
7z a score-addon-sysinfo.zip score-addon-sysinfo
