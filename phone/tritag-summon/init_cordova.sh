#!/usr/bin/env bash

rm -fr _build

cordova create _build edu.umich.eecs.lab11.tritag TriTag
pushd _build
cordova platform add android
cordova plugin add cordova-plugin-whitelist
cordova plugin add cordova-plugin-console
cordova plugin add com.randdusing.bluetoothle
cordova plugin add cordova-plugin-ble-central

pushd www

rm -r css
rm -r img
rm -r js
rm index.html

ln -s ../../css .
ln -s ../../js .
ln -s ../../index.html .

popd

cordova build

popd
