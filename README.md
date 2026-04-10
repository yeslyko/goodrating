# goodrating

goodrating is an experimental difficulty estimation algorithm for BMS SP and DP charts and various statistics, such as player leaderboard and chart difficulty leaderboard.

## Setting up

### Build Instructions:

#### Windows (Visual Studio):
Clone the repository and build it, Visual Studio should automatically hook up CMake configurations.

#### Unix:
***WIP WIP WIP WIP WIP***

### Preparing dataset
Follow the instructions from [here](https://github.com/yeslyko/goodrating#producing-dataset) to get a dataset first. The data acquiring process relies on [LR2 OxyTabler](https://crates.io/crates/lr2-oxytabler) (the table manager for Lunatic Rave 2), so make sure to get it first.

**⚠️ Warning: LR2IR does not have any DDoS protection or any protection from bots at all. As we know, the LR2IR server is hosted voluntarily by the site administrator at his home server and it's not in the best state. The scraper is used here for research purposes only, please use it with caution!**

At the end, the folder structure should look like this
```
goodrating/
    input/
        spv2/
            table-data/
                (files here are being used for the dataset preparement)
            chart_names.csv
            chart_table_levels.csv
            lr2ir_players.csv
            lr2ir_scores.csv
        dpv2/
            table-data/
                (files here are being used for the dataset preparement)
            chart_names.csv
            chart_table_levels.csv
            lr2ir_players.csv
            lr2ir_scores.csv
        dp_playerlist_lr2ir.csv 
        dp_playerlist_tachi.csv
        sp_playerlist_lr2ir.csv
        sp_playerlist_tachi.csv
    output/
        <this directory and the files inside of it will be automatically created by the program>
```

<playstyle>_playerlist_<ir>.csv files in *input* folder are optional in case if you need to get dedicated player data and recommendations, populate it with the following syntax:

``<id>,<playerName>``

playerName column is reserved for better human readability and is not used in the code. Also, playerName should be in "" if one has any special characters.

## Producing dataset

### Get table data

```bash
songdb=/path/to/song.db

table_list=$(lr2-oxytabler-dumper dump-tables-csv "$songdb")
tableid() { echo "$table_list" | grep ",$1," | awk '{print $1}' FS=','; }

mkdir -p ./input/spv2/table-data
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "ウーデオシ小学校難易度表")"    > ./input/spv2/table-data/sparmshougakkou.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "gachimijoy")"                  > ./input/spv2/table-data/spgachimijoy.json
lr2-oxytabler-dumper dump-data-json "$songdb" "$(tableid "Dystopia難易度表")"            > ./input/spv2/table-data/spdystopia.json
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
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/sparmshougakkou.json --arg table_name sparmshougakkou --arg multiplier 1.6          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spdystopia.json      --arg table_name spdystopia      --arg multiplier 0.5          --arg adj 35.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spgachimijoy.json    --arg table_name spgachimijoy    --arg multiplier 1.0          --arg adj 31.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spinsane.json        --arg table_name spinsane        --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spinsanetwo.json     --arg table_name spinsanetwo     --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spln.json            --arg table_name spln            --arg multiplier 0.9          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spluminous.json      --arg table_name spluminous      --arg multiplier 0.9          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spnormal.json        --arg table_name spnormal        --arg multiplier 1.0          --arg adj 0.5  >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spnormaltwo.json     --arg table_name spnormaltwo     --arg multiplier 1.0          --arg adj 0.5  >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spoverjoy.json       --arg table_name spoverjoy       --arg multiplier 1.0          --arg adj 31.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spsatellite.json     --arg table_name spsatellite     --arg multiplier 1.6          --arg adj 11.5 >> ./table_level_ratings_sp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/spv2/table-data/spstella.json        --arg table_name spstella        --arg multiplier 0.6          --arg adj 31.5 >> ./table_level_ratings_sp.csv

echo "TableID,Level,LevelAsInt,Rating" > ./table_level_ratings_dp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/delta.json           --arg table_name delta           --arg multiplier 1.0          --arg adj 0.5  >> ./table_level_ratings_dp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/insane.json          --arg table_name insane          --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_dp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/satellite.json       --arg table_name satellite       --arg multiplier 1.0          --arg adj 11.5 >> ./table_level_ratings_dp.csv
```

## Credits:

- [snoverpk](https://github.com/snoverpkg) - initial idea, math behind difficulty estimation algorithm
- [John Pork](https://github.com/chown2) - a lot of various code optimizations
