#!/bin/sh

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