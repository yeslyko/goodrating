# goodrating

## Producing dataset

### Get table data

```bash
songdb=/path/to/song.db

table_list=$(lr2-oxytabler-dumper dump-tables-csv "$songdb")
tableid() { echo "$table_list" | grep ",$1," | awk '{print $1}' FS=','; }

mkdir -p ./input/spv2/table-data
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "ウーデオシ小学校難易度表")"    > ./input/spv2/table-data/sparmshougakkou.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "gachimijoy")"                  > ./input/spv2/table-data/spdystopia.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Dystopia難易度表")"            > ./input/spv2/table-data/spgachimijoy.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "発狂BMS難易度表")"             > ./input/spv2/table-data/spinsane.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "NEW GENERATION 発狂難易度表")" > ./input/spv2/table-data/spinsanetwo.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "LN難易度")"                    > ./input/spv2/table-data/spln.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Luminous")"                    > ./input/spv2/table-data/spluminous.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "通常難易度表")"                > ./input/spv2/table-data/spnormal.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "NEW GENERATION 通常難易度表")" > ./input/spv2/table-data/spnormaltwo.json
# FIXME: NEW GENERATION
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Overjoy")"                     > ./input/spv2/table-data/spoverjoy.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Stella")"                      > ./input/spv2/table-data/spstella.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Satellite")"                   > ./input/spv2/table-data/spsatellite.json

mkdir -p ./input/dpv2/table-data
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "δ難易度表")"                   > ./input/dpv2/table-data/delta.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "DP Satellite")"                > ./input/dpv2/table-data/satellite.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "発狂DP難易度表")"              > ./input/dpv2/table-data/insane.json
```

### Scrape IR

```bash
my_lr2id=123456
for playstyle in spv2 dpv2; do
    jq --raw-output '[ .[].md5 ] | unique | .[]' ./input/$playstyle/table-data/*.json | sort -u > ./input/$playstyle/lr2irscraper-input.txt
    # Empty list continues from where left off.
    lr2irscraper ./input/$playstyle/lr2irscraper-output.db $my_lr2id
    # lr2irscraper ./input/$playstyle/lr2irscraper-output.db $my_lr2id ./input/$playstyle/lr2irscraper-input.txt
done
```

### Produce the data

```bash
for playstyle in spv2 dpv2; do
    echo "md5,title" > ./input/$playstyle/chart_names.csv
    jq --raw-output '[ .[] | [.md5, .title] ] | unique | .[] | @csv' ./input/$playstyle/table-data/*.json >> ./input/$playstyle/chart_names.csv
done

for playstyle in spv2 dpv2; do
    echo "table,level,md5" > ./input/$playstyle/chart_table_levels.csv
    for file in ./input/$playstyle/table-data/*.json; do
        jq --raw-output --arg table_name "$(basename -s ".json" "$file")" '[ .[] | [$table_name, .level, .md5] ] | unique | .[] | @csv' "$file" >> ./input/$playstyle/chart_table_levels.csv
    done
done

for playstyle in spv2 dpv2; do
    sqlite3 ./input/$playstyle/lr2irscraper-output.db -header -csv "SELECT lr2id, name FROM nicknames ORDER BY lr2id" > ./input/$playstyle/lr2ir_players.csv
    sqlite3 ./input/$playstyle/lr2irscraper-output.db -header -csv "SELECT md5, lr2id, lamp FROM scrapes ORDER BY lr2id" > ./input/$playstyle/lr2ir_scores.csv
done
```

## Updating seeds

If you want to update table level ratings, run the following script and then revert irrelevant updates with git.

```bash
MAKE_SKELETON_QUERY='
  [
    .[]
      | [
          $table_name,
          .level,
          ((if (.level | test("[0-9]"))
             then (.level | sub("^[^0-9]*"; "") | tonumber?)
             else (.level | tonumber? )
           end) | tonumber?) // "REPLACEME",
          ((if (.level | test("[0-9]"))
             then (.level | sub("^[^0-9]*"; "") | tonumber?)
             else (.level | tonumber? )
           end) | tonumber? * ($multiplier | tonumber) + ($adj | tonumber)) // "REPLACEME"
        ]
  ]
  | unique
  | .[]
  | @csv'

echo "TableID,Level,LevelAsInt,Rating" > ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/armshougakkou.json   --arg table_name armshougakkou   --arg multiplier 1.6          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/dystopia.json        --arg table_name dystopia        --arg multiplier 0.5          --arg adj 35.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/gachimijoy.json      --arg table_name gachimijoy      --arg multiplier 1.0          --arg adj 31.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/insane.json          --arg table_name insane          --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/insanetwo.json       --arg table_name insanetwo       --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/ln.json              --arg table_name ln              --arg multiplier 0.9          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/luminous.json        --arg table_name luminous        --arg multiplier 0.9          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/normal.json          --arg table_name normal          --arg multiplier 1.0          --arg adj 0.5  >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/normaltwo.json       --arg table_name normaltwo       --arg multiplier 1.0          --arg adj 0.5  >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/overjoy.json         --arg table_name overjoy         --arg multiplier 1.0          --arg adj 31.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/satellite.json       --arg table_name satellite       --arg multiplier 1.6          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/stella.json          --arg table_name stella          --arg multiplier 0.6          --arg adj 31.5 >> ./table_level_ratings_sp.csv

echo "TableID,Level,LevelAsInt,Rating" > ./table_level_ratings_dp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/delta.json           --arg table_name delta           --arg multiplier 1.0          --arg adj 0.5  >> ./table_level_ratings_dp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/insane.json          --arg table_name insane          --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_dp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/satellite.json       --arg table_name satellite       --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_dp.csv
```
