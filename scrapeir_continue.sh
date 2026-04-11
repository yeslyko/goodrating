#!/bin/sh

# remove this if you are not on MSYS (allows to use binaries from current directory)
export PATH=$PATH:.

my_lr2id=${1?your lr2id}

for playstyle in spv2 dpv2; do
    lr2irscrapera ./input/$playstyle/lr2irscraper-output.db "$my_lr2id"
done