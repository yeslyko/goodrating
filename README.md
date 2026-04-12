# goodrating

goodrating is an experimental difficulty estimation algorithm for BMS SP and DP charts and various statistics, such as player leaderboard and chart difficulty leaderboard.

To check for leaderboard samples, go here:

- [SP Leaderboard](https://github.com/yeslyko/goodrating/blob/master/SP_data.md)
- [DP Leaderboard](https://github.com/yeslyko/goodrating/blob/master/DP_data.md)

## Setting up

### Build Instructions:

#### Windows (Visual Studio):

Clone the repository and build it, Visual Studio should automatically hook up CMake configurations.

#### Unix:

**_WIP WIP WIP WIP WIP_**

### Preparing dataset

Follow the instructions from [here](https://github.com/yeslyko/goodrating#producing-dataset) to get a dataset first. The data acquiring process relies on [LR2 OxyTabler](https://crates.io/crates/lr2-oxytabler) (the table manager for Lunatic Rave 2) and [LR2 OxyTabler dumper](https://git.sr.ht/~showy_fence/lr2-oxytabler-dumper), so make sure to get those tools first.

**⚠️ Warning: LR2IR does not have any DDoS protection or any protection from bots at all. As we know, the site administrator hosts it at his home server and it's not in the best condition. The scraper is uploaded here for research purposes only, please use it with caution!**

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

<playstyle>_playerlist_<ir>.csv files in _input_ folder are optional in case if you need to get dedicated player data and recommendations, populate it with the following syntax:

`<id>,<playerName>`

playerName column is reserved for better human readability and is not used in the code. Also, playerName should be in "" if one has any special characters.

## Producing dataset

The scripts for producing datasets are in repository already. The order in which scripts should be run is this:

- gettabledata.sh
- scrapeir_start.sh (scrapeir_continue.sh in case if start script was interrupted)
- producedata.sh
- (optional) generateseeds.sh (only used in case if you added a new table to the gettabledata.sh, make sure to revert irrelevant updates with git after that)

## Credits:

- [snoverpk](https://github.com/snoverpkg) - initial idea, math behind difficulty estimation algorithm
- [John Pork](https://github.com/chown2) - a lot of various code optimizations
