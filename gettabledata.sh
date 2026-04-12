#!/bin/sh

# allow using binaries from current directory
export PATH=$PATH:.

songdb=${1?your path to song.db in LR2files directory} 

table_list=$(lr2-oxytabler-dumper dump-tables-csv "$songdb")
tableid() { echo "$table_list" | grep ",$1," | awk '{print $1}' FS=','; }

mkdir -p ./input/spv2/table-data
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "ウーデオシ小学校難易度表")"    > ./input/spv2/table-data/sparmshougakkou.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Dystopia難易度表")"                  > ./input/spv2/table-data/spdystopia.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "gachimijoy")"            > ./input/spv2/table-data/spgachimijoy.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "発狂BMS難易度表")"             > ./input/spv2/table-data/spinsane.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "NEW GENERATION 発狂難易度表")" > ./input/spv2/table-data/spinsanetwo.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "LN難易度")"                    > ./input/spv2/table-data/spln.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Luminous")"                    > ./input/spv2/table-data/spluminous.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "通常難易度表")"                > ./input/spv2/table-data/spnormal.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "NEW GENERATION 通常難易度表")" > ./input/spv2/table-data/spnormaltwo.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "第三期Overjoy")"                     > ./input/spv2/table-data/spoverjoy.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Stella")"                      > ./input/spv2/table-data/spstella.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Satellite")"                   > ./input/spv2/table-data/spsatellite.json

mkdir -p ./input/dpv2/table-data
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "δ難易度表")"                   > ./input/dpv2/table-data/delta.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "DP Satellite")"                > ./input/dpv2/table-data/satellite.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "発狂DP難易度表")"              > ./input/dpv2/table-data/insane.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "DP Overjoy")"					> ./input/dpv2/table-data/overjoy.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "DP Stella")"					> ./input/dpv2/table-data/stella.json
