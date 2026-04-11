#!/bin/sh

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
# overjoy7-10 is in insane 13, overjoy 11 is insane 14, overjoy 12 is insane 15
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/overjoy.json         --arg table_name overjoy         --arg multiplier 0.3          --arg adj 22.5 >> ./table_level_ratings_dp.csv
jq -r "$MAKE_SKELETON_QUERY" ./input/dpv2/table-data/stella.json          --arg table_name stella          --arg multiplier 0.3          --arg adj 22.5 >> ./table_level_ratings_dp.csv