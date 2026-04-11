#!/bin/sh

# remove this if you are not on MSYS (allows to use binaries from current directory)
export PATH=$PATH:.

my_lr2id=${1?your lr2id}

for playstyle in spv2 dpv2; do
    jq --raw-output '[ .[].md5 ] | unique | .[]' ./input/$playstyle/table-data/*.json | sort -u > ./input/$playstyle/lr2irscraper-input.txt
    lr2irscrapera ./input/$playstyle/lr2irscraper-output.db "$my_lr2id" ./input/$playstyle/lr2irscraper-input.txt
done