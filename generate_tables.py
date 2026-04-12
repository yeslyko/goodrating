import csv
import json
import sys

def tableOrder():
    return [f"☆{i/10}" for i in range(10, 121)] + [f"★{i/10}" for i in range(10, 301)] # dy11 waiting room

def main():
    if len(sys.argv) != 2:
        print("select mode sp/dp")
        return
    if sys.argv[1] == 'sp':
        mode = 'sp'
    elif sys.argv[1] == 'dp':
        mode = 'dp'
    else:
        print("select mode sp/dp")
        return

    tablesymbols = {
        "sparmshougakkou": "Ude",
        "spdystopia": "dy",
        "spgachimijoy": "双",
        "spinsane": "★",
        "spinsanetwo": "▼",
        "spln": "◆",
        "spluminous": "ln",
        "spnormal": "☆",
        "spnormaltwo": "▽",
        "spoverjoy": "★★",
        "spsatellite": "sl",
        "spstella": "st",
        "delta": "δ",
        "insane": "★",
        "satellite": "DPsl",
        "overjoy": "★★",
        "stella": "DPst"
    }

    json_ec = list()
    json_hc = list()

    with open(f'./output/{mode}/charts.csv', 'r', newline='', encoding='utf-8') as file:
        breeder = csv.reader(file, delimiter=';')
        for number, row in enumerate(breeder):
            if number == 0: # lamest way to check for a header
                continue
            adjustEC = float(row[4])
            adjustHC = float(row[5])

            is_insaneEC = adjustEC > 12
            is_insaneHC = adjustHC > 12
            
            levelstringEC = f"{"★" if is_insaneEC else "☆"}{( (adjustEC - 11) if is_insaneEC else adjustEC ):.1f}"
            levelstringHC = f"{"★" if is_insaneHC else "☆"}{( (adjustHC - 11) if is_insaneHC else adjustHC ):.1f}"

            json_ec.append(dict(level = levelstringEC, title = row[7], md5 = row[8], comment = tablesymbols[row[0]] + row[1]))
            json_hc.append(dict(level = levelstringHC, title = row[7], md5 = row[8], comment = tablesymbols[row[0]] + row[1]))

    with open(f'./output/{mode}/ec_data.json', 'w', encoding='utf-8') as ec_data_file:
        json_ec_string = json.dumps(json_ec, indent=2)
        ec_data_file.write(json_ec_string)
        ec_data_file.close()

    with open(f'./output/{mode}/hc_data.json', 'w', encoding='utf-8') as hc_data_file:
        json_hc_string = json.dumps(json_hc, indent=2)
        hc_data_file.write(json_hc_string)
        hc_data_file.close()

    with open(f'./output/{mode}/ec_header.json', 'w', encoding='utf-8') as ec_header_file:
        header_content = {
            "name": f"{mode.upper()} GOODRATING EASY CLEAR",
            "symbol": f"{mode.upper()} GRE",
            "level_order": tableOrder(),
            "data_url": "./ec_data.json"
        }
        ec_header_string = json.dumps(header_content)
        ec_header_file.write(ec_header_string)
        ec_header_file.close()

    with open(f'./output/{mode}/hc_header.json', 'w', encoding='utf-8') as hc_header_file:
        header_content = {
            "name": f"{mode.upper()} GOODRATING HARD CLEAR",
            "symbol": f"{mode.upper()} GRH",
            "level_order": tableOrder(),
            "data_url": "./hc_data.json"
        }
        hc_header_string = json.dumps(header_content)
        hc_header_file.write(hc_header_string)
        hc_header_file.close()

main()