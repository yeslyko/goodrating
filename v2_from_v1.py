import csv
import os
from collections import defaultdict


def main() -> None:
    sp = True

    chart_names = dict()
    chart_table_levels = defaultdict(lambda: defaultdict(None))
    lr2ir_players = dict()
    lr2ir_scores = defaultdict(lambda: defaultdict(None))

    def collect_table(folder: str, table_name: str) -> None:
        with open(f"./input/{folder}/{table_name}.csv") as csvfile:
            reader = csv.reader(csvfile, delimiter=";")
            for row in reader:
                level, name, md5, lr2id, lr2name, lamp = row
                if len(md5) != 32:
                    print("bad md5")
                    continue
                chart_names[md5] = name
                chart_table_levels[md5][table_name] = level  # eat poop
                lr2ir_players[lr2id] = lr2name
                match lamp:
                    case "Failed":
                        lr2ir_scores[lr2id][md5] = 1
                    case "EasyClear":
                        lr2ir_scores[lr2id][md5] = 2
                    case "NormalClear":
                        lr2ir_scores[lr2id][md5] = 3
                    case "HardClear":
                        lr2ir_scores[lr2id][md5] = 4
                    case "FullCombo":
                        lr2ir_scores[lr2id][md5] = 5
                    case _:
                        msg = f"bad lamp: {lamp}"
                        raise BaseException(msg)

    if sp:
        sp_files = [
            "sparmshougakkou",
            "spdystopia",
            "spgachimijoy",
            "spinsane",
            "spinsanetwo",
            "spln",
            "spluminous",
            "spnormal",
            "spoverjoy",
            "spsatellite",
            "spstella",
        ]
        for table in sp_files:
            collect_table("sp", table)
    else:
        dp_files = ["delta", "insane", "satellite"]
        for table in dp_files:
            collect_table("dp", table)

    out_folder = "spv2__converted" if sp else "dpv2__converted"

    os.makedirs(f"./input/{out_folder}", exist_ok=True)

    with open(f"./input/{out_folder}/chart_names.csv", "w") as f:
        f.write("md5,name\n")
        for md5, name in chart_names.items():
            f.write(md5)
            f.write(",")
            f.write(name.replace(";", "X").replace('"', "X").replace(",", "X"))
            f.write("\n")

    with open(f"./input/{out_folder}/chart_table_levels.csv", "w") as f:
        f.write("table,level,md5\n")
        for md5, table_to_level in chart_table_levels.items():
            for table, level in table_to_level.items():
                f.write(table)
                f.write(",")
                f.write(level)
                f.write(",")
                f.write(md5)
                f.write("\n")

    with open(f"./input/{out_folder}/lr2ir_players.csv", "w") as f:
        f.write("lr2id,name\n")
        for lr2id, name in lr2ir_players.items():
            f.write(lr2id)
            f.write(",")
            f.write(name.replace(";", "X").replace('"', "X").replace(",", "X"))
            f.write("\n")

    with open(f"./input/{out_folder}/lr2ir_scores.csv", "w") as f:
        f.write("lr2id,md5,clear\n")
        for lr2id, md5_to_clear in lr2ir_scores.items():
            for md5, clear in md5_to_clear.items():
                f.write(md5)
                f.write(",")
                f.write(lr2id)
                f.write(",")
                f.write(str(clear))
                f.write("\n")

    print(f"move the newly created folder now: ./input/{out_folder}")


main()
