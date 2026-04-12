#!/bin/sh

# allow using binaries from current directory
export PATH=$PATH:.

songdb=${1?your path to song.db in LR2files directory}

table_list=$(lr2-oxytabler-dumper dump-tables-csv "$songdb")
tableid() { echo "$table_list" | grep ",$1," | awk '{print $1}' FS=','; }
dump() {
	name=${1?}
	path=${2?}
	id=$(tableid "$name")
	if [ "$id" = '' ]; then
		echo "Failed to find table $name"
		return
	fi
	if ! lr2-oxytabler-dumper dump-data-json "$songdb" "$id" >"$path"; then
		echo "Failed to dump table $name"
		return
	fi
}

mkdir -p ./input/spv2/table-data
dump "ウーデオシ小学校難易度表" ./input/spv2/table-data/sparmshougakkou.json
dump "Dystopia難易度表" ./input/spv2/table-data/spdystopia.json
dump "gachimijoy" ./input/spv2/table-data/spgachimijoy.json
dump "発狂BMS難易度表" ./input/spv2/table-data/spinsane.json
dump "NEW GENERATION 発狂難易度表" ./input/spv2/table-data/spinsanetwo.json
dump "LN難易度" ./input/spv2/table-data/spln.json
dump "Luminous" ./input/spv2/table-data/spluminous.json
dump "通常難易度表" ./input/spv2/table-data/spnormal.json
dump "NEW GENERATION 通常難易度表" ./input/spv2/table-data/spnormaltwo.json
dump "第三期Overjoy" ./input/spv2/table-data/spoverjoy.json
dump "Stella" ./input/spv2/table-data/spstella.json
dump "Satellite" ./input/spv2/table-data/spsatellite.json

mkdir -p ./input/dpv2/table-data
dump "δ難易度表" ./input/dpv2/table-data/delta.json
dump "DP Satellite" ./input/dpv2/table-data/satellite.json
dump "発狂DP難易度表" ./input/dpv2/table-data/insane.json
dump "DP Overjoy" ./input/dpv2/table-data/overjoy.json
dump "DP Stella" ./input/dpv2/table-data/stella.json
