#include <algorithm>
#include <charconv>
#include <iostream>
#include <fstream>
#include <ranges>
#include <set>
#include <string>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <math.h>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <chrono>
#include <omp.h>
#include <span>

struct Player {
	std::string name;
	int lr2id;
	float rating;
	std::unordered_map<std::string, int> clears; // md5, clear
	std::string supplement;
	// PERF: vector performs better than unordered_map on the amount of levels tables usually have.
	std::unordered_map<std::string, std::vector<std::pair<int, int>>> completionList;
};

struct Chart {
	std::string name;
	std::unordered_map<std::string, int> tablesFolders;
	float rating;
	float hcrating;
	std::vector<std::pair<int, int>> scores; // [lr2id][clearType]
	int playcount;
	float cleardiffsd;
};

static constexpr auto&& lr2irplayers = {
	85349, //cardinal
	3906, //rag nihongo wakaranai
	147174, //zyxwe
	156698, //lyko
	74079, //BLAZE
	162258, //cat
	21634, //ABCD
	4075, //cheater0133
};

static constexpr auto&& bokutachiplayers = {
	"t49", //pupu
	"t374", //snover
	"t429", //zyxwe
	"t50", //tobycool
	"t39", //nyannurs
	"t1305", //mat
	"t1642", //converstation
	"t263", //paprotka
	"t467", //tokakitake
};

static constexpr auto&& cheatersList =
{
	122738, //JADONG_GOD
	114328, //JADONG
	159674, //meumeu7
	162280, //Chieri-Kata
	111023, //不正
	108312, //SS Officer
	113338, //FUGAGOOD
	104837, //FUGAFUCK
	153667, //0133
	141249, //Ta2
	142961, //Amluox
	145628, //OJ.Amluox
	139857, //Pazo
	111571, //AiLee
	183696, //zionfan
};

template<typename T>
static std::optional<T> from_chars(std::string_view s)
{
	T out;
	if (auto ec = std::from_chars(s.data(), s.data() + s.size(), out);
			ec.ec != std::errc{} || ec.ptr != s.data() + s.size())
		return std::nullopt;
	return {out};
}

static std::unordered_map<int, Player> playerTable;
static std::unordered_map<std::string, Player> tachiPlayerTable;
static std::unordered_map<std::string, Chart> songTable;
// [table][folder] - amount of charts
// PERF: vector performs better than unordered_map on the amount of levels tables usually have.
static std::unordered_map<std::string, std::vector<std::pair<int, int>>> tableTable;

static int mode;

static bool checkForTable(const std::string& table, const Chart& chart) {
	return chart.tablesFolders.contains(table);
}

static int clearConversion(const std::string& clearType) {
	if (clearType == "Failed") return 0;
	if (clearType == "EasyClear") return 1;
	if (clearType == "NormalClear") return 1;
	if (clearType == "HardClear") return 2;
	if (clearType == "FullCombo") return 2;
	return -1;
}

static float clearProbability(float pr, float cr) {
	float sigma = ((mode == 1) ? 1.F : 0.5F);
	return (0.5F * (1.F + std::erf((pr - cr) / (sqrt(2.F) * sigma))));
}

//measurement of distance between player and chart rating; high-rated players with fails on low-rated charts should not bring the ratings up although the inverse does apply
static float calcRelevance(float pr, float cr) {
	float sigma = ((mode == 1) ? 1.F : 0.5F);
	return -(0.5F * (std::erf((pr - cr) / (sqrt(2.F) * sigma)) - 1.F));
	//return 1.F / (1.F + std::exp(std::pow(pr - cr, 2.F)));
}

static float chartEstimator(float CR, float PR, int clear, int mode) {
	switch (mode) {
	case 0:
		if (clear) clear = 1;
		break;
	case 1:
		if (clear != 2) clear = 0;
		if (clear == 2) clear = 1;
		break;
	default: abort();
	}

	//nudge uwu
	return clearProbability(PR, CR) - static_cast<float>(clear);
}

static void playerEstimator(Player& player) {
	std::vector<float> clearRatings;
	std::unordered_map<std::string, Chart>::iterator urg;

	int clears = 0;
	for (auto & [md5, clear] : player.clears) {
		if (clear > 0) {
			clears++;
			urg = songTable.find(md5);
			if (urg == songTable.end()) continue;
			if (clear == 2) {
				clearRatings.push_back(urg->second.hcrating);
			}
			else {
				clearRatings.push_back(urg->second.rating);
			}
		}
		continue;
	}

	if (clears == 0) {
		player.rating = -999;
		return;
	}

	std::ranges::sort(clearRatings, std::greater<float>());

	//average of exponentially weighted top scores, this produces the result i want assuming a random score distribution therefore it is good
	float rate = 0;
	int size = std::min(static_cast<int>(clearRatings.size()), 50);
	for (int j = 0; j < size; j++) {
		rate += std::exp(clearRatings[j]);
	}
	player.rating = std::max(std::log(rate / static_cast<float>(size)), 0.F);
}

//normalize ratings to fit folder ratings for tachi
static float adjRating(float rating, std::vector<std::pair<float, float>>* normalizer) {
	if (rating < normalizer->at(0).first) {
		return rating + normalizer->at(0).second;
	}
	if (rating > (normalizer->end() - 1)->first) {
		float peach = (normalizer->end() - 1)->first - (normalizer->end() - 2)->first;
		float caravan = rating - (normalizer->end() - 1)->first;
		float stretcher = 2.F;
		return (normalizer->end() - 1)->first + (caravan / peach) * stretcher;
	}

	for (size_t i = 1; i < normalizer->size(); i++) {
		if (normalizer->at(i).first >= rating && normalizer->at(i - 1).first < rating) {
			float range = normalizer->at(i).first - normalizer->at(i - 1).first;
			float prop = (rating - normalizer->at(i - 1).first) / range;
			float lerp = prop * normalizer->at(i).second + (1 - prop) * normalizer->at(i - 1).second;
			return rating + lerp;
		}
	}

	return -999;
}

static bool chartReader(const std::string& filename, const std::string& table) {
	std::ifstream file(filename);
	if (!file.is_open()) {
		std::cerr << "deez nuts";
		return true;
	}

	std::string gotline;
	int i = 0;
	int folder;
	std::string songname;
	std::string sid;
	int pid = 0;
	std::string playername;
	std::string cleartype;

	while (std::getline(file, gotline)) {
		if (!gotline.empty() && gotline.back() == '\r') // on Linux on Linux getline splits on \n but files may be \r\n
			gotline.pop_back();
		for (auto&& line_ : std::views::split(gotline, ';')) {
			const auto line = std::string_view{line_.begin(), line_.end()};
			switch (i) {
			case 0:
				switch(mode)
				{
				case 1:
					if (line == "ï¿½H" || line == "99" ||
							line == "?" || line == "???" ||
							line == "査定中" || line == "999" || line == "X") {
						folder = -1;
					}
					else if (line == "0-" || line == "-2" || line == "-1" || line == "DELAY_BEGINNER") {
						folder = 0;
					}
					else if (line == "DELAY_MASTER") {
						folder = 13;
					}
					else if (line == "11+" || line == "12-" || line == "12+") {
						folder = 12;
					}
					else {
						try {
							folder = from_chars<int>(line).value();
						}
						catch (const std::exception& e) {
							std::cout << line << ": " << e.what() << '\n';
						}
					}
					break;
				case 2:
					if (line == "�H" || line == "99" || line == "?") {
						folder = -1;
					}
					else {
						try {
							folder = from_chars<int>(line).value();
						}
						catch (const std::exception& e) {
							std::cout << line << ": " << e.what() << '\n';
						}
					}
					break;
				default: abort(); break;
				}
				i++;
				break;
			case 1:
				songname = line;
				i++;
				break;
			case 2:
				sid = line;
				i++;
				break;
			case 3:
				pid = from_chars<int>(line).value();
				i++;
				break;
			case 4:
				playername = line;
				i++;
				break;
			case 5:
				cleartype = line;
				i = 0;
				break;
			default: abort();
			}

			if (i != 0 || std::ranges::contains(cheatersList, pid))
				continue;

			int clearVal = clearConversion(cleartype);

			auto got = playerTable.find(pid);
			if (got == playerTable.end()) {
				Player player;
				player.name = playername;
				player.lr2id = pid;
				player.rating = 0;
				player.clears.insert_or_assign(sid, clearVal);
				playerTable.emplace(pid, player);
			}
			else {
				auto& clear = got->second.clears[sid];
				clear = std::max(clear, clearVal);
			}

			auto get = songTable.find(sid);
			if (get == songTable.end()) {
				Chart chart;
				chart.name = songname;
				chart.tablesFolders.emplace(table, folder);
				chart.rating = -1;
				if (mode == 1) {
					if (table == "spnormal" ||
							table == "spnormaltwo") {
						chart.rating = static_cast<float>(folder) + 0.5F;
					}
					if (table == "spinsane" ||
							table == "spinsanetwo") {
						chart.rating = static_cast<float>(folder) + 11.5F;
					}
					if (table == "spsatellite" ||
							table == "sparmshougakkou") {
						chart.rating = static_cast<float>(folder) * 1.6F + 11.5F;
					}
					if (table == "spln" ||
							table == "spluminous") {
						chart.rating = static_cast<float>(folder) * 0.9F + 11.5F;
					}
					if (table == "spstella") {
						chart.rating = static_cast<float>(folder) * 0.6F + 31.5F;
					}
					if (table == "spoverjoy" ||
							table == "spgachimijoy") {
						chart.rating = static_cast<float>(folder) + 31.5F;
					}
					if (table == "spdystopia") {
						chart.rating = static_cast<float>(folder) * 0.5F + 35.5F;
					}
				}
				else if (mode == 2) {
					if (table == "delta") {
						chart.rating = static_cast<float>(folder) + 0.5F;
					}
					if (table == "insane" || table == "satellite") {
						chart.rating = static_cast<float>(folder) + 11.5F;
					}
				}
				/*
				   if (table == "overjoy" || table == "stella") {
				   chart.rating = 23.5F;
				   }
				   if (folder == -1) {
				   if (table == "overjoy") chart.rating = 23.5F;
				   if (table == "insane") chart.rating = 16.5F;
				   if (table == "delta") chart.rating = 6.5F;
				   }
				   */
				chart.hcrating = chart.rating;
				chart.scores.emplace_back(pid, clearVal);
				chart.playcount++;
				songTable.emplace(sid, chart);
			}
			else {
				if (!(checkForTable(table, get->second))) get->second.tablesFolders.emplace(table, folder);
				get->second.scores.emplace_back(pid, clearVal);
				get->second.playcount++;
			}

		}
	}


	return false;
}

static float scaler = 1.F;
static float summer = 0.F;

//calc values for normalizing ratings to folders
static void calcImportantFolderAverages() {
	std::string normal;
	std::string insane;

	if (mode == 1) {
		normal = "spnormal";
		insane = "spinsane";
	}
	else if (mode == 2) {
		normal = "delta";
		insane = "insane";
	}

	float eep = 0;
	int womp = 0;
	for (auto & charts : songTable) {
		if (checkForTable(normal, charts.second) && charts.second.tablesFolders.find(normal)->second == 1) {
			eep += charts.second.rating;
			womp++;
		}
	}
	summer = eep / static_cast<float>(womp);

	float wah = 0;
	int glomp = 0;
	for (auto & charts : songTable) {
		if (checkForTable(insane, charts.second) && charts.second.tablesFolders.find(insane)->second == ((mode == 1) ? 25 : 13)) {
			wah += charts.second.rating + summer;
			glomp++;
		}
	}
	scaler = ((mode == 1) ? 36.5F : 24.5F) / (wah / static_cast<float>(glomp));
}

static std::vector<std::pair<float, float>> folderNormalizer;

static void calcFolderNormalizers(std::vector<std::pair<float, float>>* folderNormalizer) {
	//add normalization constants for each folder
	std::string normal;
	std::string insane;

	if (mode == 1) {
		normal = "spnormal";
		insane = "spinsane";
	}
	else if (mode == 2) {
		normal = "delta";
		insane = "insane";
	}

	for (int i = 1; i < ((mode == 1) ? 37 : 25); i++) {
		float flotsam = 0;
		int count = 0;
		for (auto & charts : songTable) {
			if (!(checkForTable(normal, charts.second) || checkForTable(insane, charts.second))) {
				continue;
			}
			int folder = [&]() {
				auto normal_it = charts.second.tablesFolders.find(normal);
				if (normal_it != charts.second.tablesFolders.end())
					return normal_it->second;
				return charts.second.tablesFolders.at(insane);
			}();
			if (checkForTable(normal, charts.second) && folder > 11) continue;
			if (checkForTable(insane, charts.second)) folder += 11;
			if (folder != i) {
				continue;
			}
			flotsam += (charts.second.rating + summer) * scaler;
			count++;
		}
		float avg = flotsam / static_cast<float>(count);
		folderNormalizer->emplace_back(avg, (static_cast<float>(i) + 0.5F) - avg);
	}
}

static float calcFailWeight(const Player& player, const Chart& chart) {
	float failWeight = 0;

	for (const auto& [table, level] : chart.tablesFolders) {
		int chartCount = std::ranges::find(tableTable.at(table), level, &std::pair<int, int>::first)->second;
		int playCount = std::ranges::find(player.completionList.at(table), level, &std::pair<int, int>::first)->second;
		failWeight = std::max(failWeight, static_cast<float>(playCount) / static_cast<float>(chartCount));
	}

	return failWeight;
}

//used for tethering ratings to the average of its folder
static void calcTableAverages(std::unordered_map<std::string, std::pair<int, float>>& tableAverages)
{
	tableAverages.clear();
	for (auto& [_md5, chart] : songTable) {
		if (chart.tablesFolders.empty()) {
			if(static bool once = false; !once) {
				once = true;
				std::cout << "your dataset has a chart present in no tables\n";
			}
			continue;
		}
		const auto& [name, level] = *chart.tablesFolders.begin();
		auto& [a, b] = tableAverages[name + std::to_string(level)];
		a += 1;
		b += chart.rating;
	}
	for (auto& [_k, averages] : tableAverages) {
		averages.second /= static_cast<float>(averages.first);
	}
}

static void writePlayerData(const Player& player, bool useSupplement) {
	std::error_code ec;
	std::filesystem::create_directories(mode == 1 ? "output/sp/playerData" : "output/dp/playerData", ec);
	if (ec) {
		std::cout << "failed to create directory: " << ec.message() << '\n';
		return;
	}
	std::string path = ((mode == 1) ? "output/sp/playerData/" : "output/dp/playerData/") + (useSupplement ? player.supplement : std::to_string(player.lr2id)) + ".csv";
	std::ofstream playerData(path);
	if (!playerData.is_open()) {
		std::cout << "!playerData.is_open()\n";
		return;
	}
	playerData << "md5;chart name;rating;hcrating;adjRating;adjHcRating;clear.second" << '\n';
	for (auto& [md5, clear] : player.clears) {
		auto chartIter = songTable.find(md5);
		if (chartIter == songTable.end()) continue;
		const Chart& charting = chartIter->second;
		playerData << chartIter->first << ";" << charting.name << ";" << charting.rating << ";" <<
			charting.hcrating << ";" << adjRating((charting.rating + summer) * scaler, &folderNormalizer)
			<< ";" << adjRating((charting.hcrating + summer) * scaler, &folderNormalizer) << ";" << clear
			<< '\n';
	}
	std::cout << "wrote player data for " << (useSupplement ? player.supplement : std::to_string(player.lr2id)) << '\n';
}

static float guessRating(Chart& chart) {
	bool isCleared = false;
	float minRating = 999.F;
	float maxRating = -999.F;
	for (auto s : chart.scores) {
		if (s.second != 0) isCleared = true;
	}
	/*
	if (chart.scores.size() < 5) {
		if (!isCleared) return -999;
	}
	*/
	if (isCleared) {
		for (auto s : chart.scores) {
			if (s.second == 0) continue;
			minRating = std::min(playerTable.find(s.first)->second.rating, minRating);
		}
		return minRating;
	}
	else {
		for (auto s : chart.scores) {
			maxRating = std::max(playerTable.find(s.first)->second.rating, maxRating);
		}
		return maxRating;
	}
}

static void countFolderCompletions() {
	for (auto& [_id, player] : playerTable) {
		for (const auto& [md5, _clear] : player.clears) {
			for (const auto& [name, level] : songTable.at(md5).tablesFolders) {
				auto& l = player.completionList[name];
				if (auto it = std::ranges::find(l, level, &std::pair<int, int>::first); it != std::end(l)) {
					it->second += 1;
				} else {
					l.emplace_back(level, 1);
				}
			}
		}
	}
}

static void countChartCount() {
	for (const auto& [_md5, chart] : songTable) {
		for (const auto& [name, level] : chart.tablesFolders) {
			auto& l = tableTable[name];
			if (auto it = std::ranges::find(l, level, &std::pair<int, int>::first); it != std::end(l)) {
				it->second += 1;
			} else {
				l.emplace_back(level, 1);
			}
		}
	}
}

// RFC 4180 CSV
// \return field count
[[nodiscard]] static size_t split_csv(std::vector<std::string>& buf, std::string_view s) {
	if (buf.empty()) {
		buf.emplace_back();
	} else {
		for (auto& line : buf) { // keep per-line capacity around
			line.clear();
		}
	}
	// Something is funky with quotes but I don't care enough since it's only used for cosmetics. TODO: look for
	// song name of 179ca83e83a13dceabb6fbcd093aa33c.
	size_t field_index = 0;
	bool prev_is_quote = false;
	bool inside_quote = false;
	for (char c : s) {
		if (c == '"') {
			if (inside_quote && prev_is_quote) {
				// escaped quote
				prev_is_quote = false;
				buf[field_index].push_back(c);
			} else {
				inside_quote = !inside_quote;
				prev_is_quote = true;
			}
		} else if (c == ',' && !inside_quote) {
			prev_is_quote = false;
			field_index++;
			if (buf.size() < field_index + 1) {
				buf.emplace_back();
			}
		} else {
			prev_is_quote = false;
			buf[field_index].push_back(c);
		}
	}
	return field_index + 1;
}

// \return non-empty string on error
static std::string load_dataset_v2(int mode, std::unordered_map<int, Player>& playerTable,
				   std::unordered_map<std::string, Chart>& songTable)
{
	int parts_loaded = 0;
	static constexpr int parts_total = 4;

	std::string line_buf;
	std::vector<std::string> csv_buf;

	std::unordered_map<std::string, std::unordered_map<std::string, int>> table_level_as_int;
	std::unordered_map<std::string, std::unordered_map<std::string, float>> table_level_rating;
	if (auto ifs = std::ifstream{mode == 1 ? "table_level_ratings_sp.csv" : "table_level_ratings_dp.csv"}; ifs.is_open())
	{
		bool skip_first = true;
		size_t line = 0;
		while (std::getline(ifs, line_buf)) {
			line++;
			if (skip_first) {
				skip_first = false;
				continue;
			}
			if (line_buf.starts_with('#')) {
				continue;
			}
			if (!line_buf.empty() && line_buf.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				line_buf.pop_back();
			if (auto z = split_csv(csv_buf, line_buf); z != 4) {
				return std::to_string(line) + ": invalid csv field count in table level ratings: " + std::to_string(z) + " != " + std::to_string(4);
			}
			const std::string& table = csv_buf[0];
			const std::string& level = csv_buf[1];
			const auto level_as_int = from_chars<int>(csv_buf[2]);
			if (!level_as_int) {
				return std::to_string(line) + ": invalid 'level_as_int'";
			}
			const auto rating = from_chars<float>(csv_buf[3]);
			if (!rating) {
				return std::to_string(line) + ": invalid 'rating'";
			}
			table_level_as_int[table][level] = *level_as_int;
			table_level_rating[table][level] = *rating;
		}
	}

	if (auto ifs = std::ifstream{mode == 1 ? "input/spv2/chart_names.csv" : "input/dpv2/chart_names.csv"}; ifs.is_open())
	{
		parts_loaded += 1;
		bool skip_first = true;
		size_t line = 0;
		while (std::getline(ifs, line_buf)) {
			line++;
			if (skip_first) {
				skip_first = false;
				continue;
			}
			if (line_buf.starts_with('#')) {
				continue;
			}
			if (!line_buf.empty() && line_buf.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				line_buf.pop_back();
			if (auto z = split_csv(csv_buf, line_buf); z != 2) {
				return std::to_string(line) + ": invalid csv field count in chart names: " + std::to_string(z) + " != " + std::to_string(2);
			}
			const std::string& md5 = csv_buf[0];
			if (md5.size() != 32) {
				return std::to_string(line) + ": invalid md5: " + md5;
			}
			const std::string& name = csv_buf[1];
			auto& chart = songTable[md5];
			chart.name = name;
			for (char c : {';', ',', '"'})
				for(auto pos = chart.name.find(c); pos != chart.name.npos; pos = chart.name.find(c))
					chart.name[pos] = 'X';
			chart.rating = -1;
			chart.hcrating = -1;
		}
	}
	if (auto ifs = std::ifstream{mode == 1 ? "input/spv2/chart_table_levels.csv" : "input/dpv2/chart_table_levels.csv"}; ifs.is_open())
	{
		parts_loaded += 1;
		bool skip_first = true;
		size_t line = 0;
		while (std::getline(ifs, line_buf)) {
			line++;
			if (skip_first) {
				skip_first = false;
				continue;
			}
			if (line_buf.starts_with('#')) {
				continue;
			}
			if (!line_buf.empty() && line_buf.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				line_buf.pop_back();
			if (auto z = split_csv(csv_buf, line_buf); z != 3) {
				return std::to_string(line) + ": invalid csv field count in chart table levels: " + std::to_string(z) + " != " + std::to_string(3);
			}

			const std::string& table = csv_buf[0];
			const std::string& level = csv_buf[1];
			const std::string& md5 = csv_buf[2];

			if (md5.size() != 32) {
				return std::to_string(line) + ": invalid md5: " + md5;
			}

			auto chart = songTable.find(md5);
			if (chart == songTable.end()) {
				return std::to_string(line) + ": song '" + md5 + "' mention in  was not found";
			}
			chart->second.tablesFolders[table] = table_level_as_int[table][level];
			// inherited from non-v2 but this is stupid as it may overwrite rating depending on
			// order of data
			chart->second.rating = table_level_rating[table][level];
			chart->second.hcrating = chart->second.rating;
		}
	}
	if (auto ifs = std::ifstream{mode == 1 ? "input/spv2/lr2ir_players.csv" : "input/dpv2/lr2ir_players.csv"}; ifs.is_open())
	{
		parts_loaded += 1;
		bool skip_first = true;
		size_t line = 0;
		while (std::getline(ifs, line_buf)) {
			line++;
			if (skip_first) {
				skip_first = false;
				continue;
			}
			if (line_buf.starts_with('#')) {
				continue;
			}
			if (!line_buf.empty() && line_buf.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				line_buf.pop_back();
			if (auto z = split_csv(csv_buf, line_buf); z != 2) {
				return std::to_string(line) + ": invalid csv field count in lr2ir players: " + std::to_string(z) + " != " + std::to_string(2);
			}
			auto lr2id = from_chars<int>(csv_buf[0]);
			if (!lr2id) {
				return std::to_string(line) + ": invalid 'lr2id'";
			}
			const std::string& name = csv_buf[1];
			if (std::ranges::contains(cheatersList, *lr2id)) {
				continue;
			}
			auto& player = playerTable[*lr2id];
			player.name = name;
			for (char c : {';', ',', '"'})
				for(auto pos = player.name.find(c); pos != player.name.npos; pos = player.name.find(c))
					player.name[pos] = 'X';
			player.lr2id = *lr2id;
		}
	}
	if (auto ifs = std::ifstream{mode == 1 ? "input/spv2/lr2ir_scores.csv" : "input/dpv2/lr2ir_scores.csv"}; ifs.is_open())
	{
		parts_loaded += 1;
		bool skip_first = true;
		size_t line = 0;
		while (std::getline(ifs, line_buf)) {
			line++;
			if (skip_first) {
				skip_first = false;
				continue;
			}
			if (line_buf.starts_with('#')) {
				continue;
			}
			if (!line_buf.empty() && line_buf.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				line_buf.pop_back();
			if (auto z = split_csv(csv_buf, line_buf); z != 3) {
				return std::to_string(line) + ": invalid csv field count in lr2ir scores: " + std::to_string(z) + " != " + std::to_string(3);
			}
			const std::string& md5 = csv_buf[0];
			auto lr2id = from_chars<int>(csv_buf[1]);
			auto lamp = from_chars<int>(csv_buf[2]);

			if (std::ranges::contains(cheatersList, *lr2id)) {
				continue;
			}

			auto player = playerTable.find(*lr2id);
			if (player == playerTable.end()) {
				return std::to_string(line) + ": score for missing player: " + std::to_string(*lr2id) + " " + md5;
			}

			auto chart = songTable.find(md5);
			if (chart == songTable.end()) {
				return std::to_string(line) + ": score for missing song: " + std::to_string(*lr2id) + " " + md5;
			}

			auto& clear = player->second.clears[md5];
			switch (*lamp) {
				case 0: 		  // noplay
				case 1: clear = 0; break; // fail
				case 2: 		  // easy
				case 3: clear = 1; break; // groove
				case 4:                   // hard
				case 5: clear = 2; break; // fc
				default: std::cout << std::to_string(line) + ": bad lamp " << *lamp << ' ' << *lr2id << ' ' << md5 << '\n'; continue;
			}

			// like above, dumb in case there appear several lamps from same player
			chart->second.scores.emplace_back(*lr2id, clear);
			chart->second.playcount++;
		}
	}

	if (parts_loaded != parts_total) {
		return "missing data, loaded " + std::to_string(parts_loaded) + '/' + std::to_string(parts_total) + " parts";
	}

	return {};
}

static bool runFullIterations(bool enable_v2_data) {
	if (enable_v2_data) {
		std::cout << "loading v2 data..." << '\n';
		auto beg = std::chrono::high_resolution_clock::now();
		if(auto error = load_dataset_v2(mode, playerTable, songTable); !error.empty()){
			std::cout << "loading data failed: " << error << '\n';
			return true;
		}
		std::cout << "loaded data in "
			<< std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - beg).count()
			<< " seconds\n";
	} else {
		std::cout << "loading data..." << '\n';
		auto beg = std::chrono::high_resolution_clock::now();
		for (const auto& dirEntry : std::filesystem::directory_iterator((mode == 1) ? "input/sp/" : "input/dp/")) {
			std::string stem = std::filesystem::path(dirEntry).stem().string();
			std::cout << "loading table " << stem << '\n';
			if (chartReader(std::filesystem::path(dirEntry).string(), stem)) return true;
		}
		std::cout << "loaded data in "
			<< std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - beg).count()
			<< " seconds\n";
	}

	countFolderCompletions();
	countChartCount();

	int iter = 100; // amount of iterations
	if (const char* my_iter_count = getenv("MY_ITER_COUNT")) { // NOLINT(concurrency-mt-unsafe) poop
		iter = from_chars<int>(my_iter_count).value();
	}
	int helper = iter;
	//nudge size per iteration
	float scale = 0.2F;
	float scaleScaler = 1.F;
	//tether strength to folder average
	float fether = 0.5F;

	//this just makes the mean more stable i don't know the consequences of this but the minimum player rating is 0 so don't let that happen too much :)
	const float bad = 0.05F;

	std::cout << "running iterations..." << '\n';

	auto iterstart = std::chrono::high_resolution_clock::now();

	size_t totalCharts = 0;
	float ecMean = 0;
	float ecSigma = 0;
	float prevMean, prevSigma;

	std::vector<std::string> removeList;
	std::set<int> removePlayerList;

	bool firstRun = true;

	std::unordered_map<std::string, std::pair<int, float>> tableAverages;

	std::vector<Player*> playerPtrs;

	//run estimation algorithm iterations
	while (iter) {
		scale *= scaleScaler;
		scale = std::max(scale, 0.02F);

		prevMean = ecMean;
		prevSigma = ecSigma;

		totalCharts = 0;
		playerPtrs.clear();
		playerPtrs.reserve(playerTable.size());
		for (auto& kv : playerTable) {
			playerPtrs.push_back(&kv.second);
		}
#pragma omp parallel for schedule(static)
		// NOLINTNEXTLINE(modernize-loop-convert) openmp
		for (int i = 0; i < static_cast<int>(playerPtrs.size()); ++i) {
			playerEstimator(*playerPtrs[i]);
		}
		if (firstRun) {
			for (auto& g : songTable) {
				if (g.second.rating != -1) continue;
				float rating = guessRating(g.second);
				if (rating == -999) {
					removeList.push_back(g.first);
					continue;
				}
				g.second.rating = rating;
				g.second.hcrating = rating;
			}
			for (auto& nodata : songTable) {
				if (std::ranges::contains(removeList, nodata.first))
					continue;
				if (nodata.second.scores.size() < 5) {
					if (guessRating(nodata.second) == -999) {
						removeList.push_back(nodata.first);
					}
				}
			}
			for (const auto& r : removeList) {
				songTable.erase(r);
			}
			for (auto p : playerPtrs) {
				if (p->rating == -999) removePlayerList.insert(p->lr2id);
			}
			for (auto noob : removePlayerList) {
				playerTable.erase(noob);
			}
			for (auto& [_md5, chart] : songTable) {
				std::erase_if(chart.scores, [&removePlayerList](std::pair<int, int> player_and_clear){
					return removePlayerList.contains(player_and_clear.first);
					});
			}
			firstRun = false;
			removeList.clear();
			removePlayerList.clear();
		}

		//run ec ratings for each file
		calcTableAverages(tableAverages);

#pragma omp parallel
		{
			float localEcMean = 0;

#pragma omp for
			for (int i = 0; i < static_cast<int>(songTable.size()); ++i) {
				auto it = std::next(songTable.begin(), i);
				Chart& chart = it->second;
				float sum = 0.f;
				float cr = chart.rating;
				float totalRelevance = 1.F;
				float relevance = 0.F;

				for (size_t k = 0; k < chart.scores.size(); k++) {
					const Player& player = playerTable.at(chart.scores[k].first);
					float pr = player.rating;
					float failWeight = calcFailWeight(player, chart);
					relevance = calcRelevance(pr, cr) * ((chart.scores[k].second == 0) ? failWeight : 1);
					if ((pr < cr) && (chart.scores[k].second > 0)) relevance += cr - pr;
					sum += scale * chartEstimator(cr, pr, chart.scores[k].second, 0) * relevance;
					totalRelevance += relevance;
				}

				const auto& [name, level] = *chart.tablesFolders.begin();
				sum -= (1.F - 2.F * clearProbability(tableAverages.at(name + std::to_string(level)).second,
					chart.rating)) * fether;
				sum /= totalRelevance;
				sum += bad;
				chart.rating += sum;
				localEcMean += chart.rating;
			}

#pragma omp critical
			{
				ecMean += localEcMean;
			}
		}

		totalCharts += songTable.size();
		ecMean /= static_cast<float>(totalCharts);
		ecSigma = 0;

		//run hc ratings for each file
#pragma omp parallel
		{
			float localEcSigma = 0;

#pragma omp for
			for (int i = 0; i < static_cast<int>(songTable.size()); ++i) {
				auto it = std::next(songTable.begin(), i);
				Chart& chart = it->second;
				float sum = 0.f;
				float cr = chart.hcrating;
				float clearsd = 0.f;
				int clearpc = 0;
				float totalRelevance = 1.F;
				float relevance = 0.F;

				for (size_t k = 0; k < chart.scores.size(); k++) {
					const Player& player = playerTable.find(chart.scores[k].first)->second;
					float pr = player.rating;
					float failWeight = calcFailWeight(player, chart);
					relevance = calcRelevance(pr, cr) * ((chart.scores[k].second < 2) ? failWeight : 1);
					if ((pr < cr) && (chart.scores[k].second == 2)) relevance += cr - pr;
					sum += scale * chartEstimator(cr, pr, chart.scores[k].second, 1) * relevance;
					totalRelevance += relevance;

					if ((((pr < cr) && (chart.scores[k].second > 0)) || ((pr >= cr) && (chart.scores[k].second == 0))) &&
						(std::abs(pr - cr) < 5.F)) {
						clearpc++;
						clearsd += std::pow((cr - pr), 2.F);
					}
				}

				chart.cleardiffsd = std::sqrt(clearsd / static_cast<float>(std::max(clearpc, 1)));
				const auto& [name, level] = *chart.tablesFolders.begin();
				sum -= (1.F - 2.F * clearProbability(tableAverages.at(name + std::to_string(level)).second,
					chart.hcrating)) * fether;
				sum /= totalRelevance;
				sum += bad;
				chart.hcrating += sum;
				localEcSigma += std::pow(chart.rating - ecMean, 2.F);
			}

#pragma omp critical
			{
				ecSigma += localEcSigma;
			}
		}
		iter--;

		std::cout << "(" << helper - iter << "/" << helper << ") iterations completed..." << '\n';
		std::cout << "x: " << ecMean << " - s: " << std::sqrt(ecSigma / static_cast<float>(totalCharts)) << " - " << ecMean - prevMean << ", " << std::sqrt(ecSigma / static_cast<float>(totalCharts)) - std::sqrt(prevSigma / static_cast<float>(totalCharts)) << '\n';
	}

	auto iterend = std::chrono::high_resolution_clock::now();

	std::chrono::duration<double> s_double = iterend - iterstart;

	std::cout << helper << " iterations completed in " << s_double.count() << " seconds.\n";

	calcImportantFolderAverages();
	calcFolderNormalizers(&folderNormalizer);

	std::error_code ec;
	std::filesystem::create_directories(mode == 1 ? "output/sp" : "output/dp", ec);
	if (ec) {
		std::cout << "failed to create directory: " << ec.message() << '\n';
		return true;
	}

	std::ofstream CRTable((mode == 1) ? "output/sp/charts.csv" : "output/dp/charts.csv");
	if (!CRTable.is_open()) {
		std::cout << "!CRTable.is_open()\n";
		return true;
	}
	CRTable << "table;level3;rating;hcrating;adjEC;adjHC;cleardiffsd;name;md5\n";
	for (auto & charts : songTable) {
		float adjEC = adjRating((charts.second.rating + summer) * scaler, &folderNormalizer);
		float adjHC = adjRating((charts.second.hcrating + summer) * scaler, &folderNormalizer);
		std::string folderCheck;
		if (charts.second.tablesFolders.begin()->second == -1) folderCheck = "?"; else folderCheck = std::to_string(charts.second.tablesFolders.begin()->second);
		CRTable << charts.second.tablesFolders.begin()->first << ";" << folderCheck << ";" <<
			charts.second.rating << ";" << charts.second.hcrating << ";" << adjEC << ";" << adjHC << ";" <<
			charts.second.cleardiffsd << ";" << charts.second.name << ";" << charts.first << "\n";
	}
	CRTable.close();

	std::ofstream PRTable((mode == 1) ? "output/sp/players.csv" : "output/dp/players.csv");
	if (!PRTable.is_open()) {
		std::cout << "!PRTable.is_open()\n";
		return true;
	}
	PRTable << "rating;adjRating;lr2id;name\n";
	for (auto & players : playerTable) {
		if (players.second.rating == -999) continue;
		float adjRate = adjRating((players.second.rating + summer) * scaler, &folderNormalizer);
		PRTable << players.second.rating << ";" << adjRate << ";" << players.first << ";" << players.second.name << "\n";
	}
	PRTable.close();

	std::ofstream stats((mode == 1) ? "output/sp/stats.csv" : "output/dp/stats.csv");
	if (!stats.is_open()) {
		std::cout << "!stats.is_open()\n";
		return true;
	}
	stats << "summer;scaler\n";
	stats << summer << ";" << scaler << '\n';
	for (auto [summer, scaler] : folderNormalizer) {
		stats << summer << ";" << scaler << '\n';
	}
	stats.close();

	std::cout << "writing player data...\n";
	for (int lr2id : lr2irplayers) {
		if (auto player = playerTable.find(lr2id); player != playerTable.end()) {
			writePlayerData(player->second, false);
		}
	}

	return false;
}

static void calcOtherIRScores(const std::string& path, const std::string& supplement) {
	// if (songTable.size() == 0) loadSongs();
	if (!std::filesystem::exists(path)) {
		std::cout << "calcOtherIRScores - path doesn't exist: " << path << "\n";
		return;
	}

	std::error_code ec;
	std::filesystem::create_directories(mode == 1 ? "output/sp" : "output/dp", ec);
	if (ec) {
		std::cout << "failed to create directory: " << ec.message() << '\n';
		return;
	}

	std::ofstream players((mode == 1) ? "output/sp/tachiPlayers.csv" : "output/dp/tachiPlayers.csv");

	if (!players.is_open()) {
		std::cout << "!players.is_open()\n";
		return;
	}

	players << "rating;adjRating;id;name\n";

	std::stringstream ss;

	std::string nut;
	std::string line;
	std::string md5;
	for (const auto& dirEntry : std::filesystem::directory_iterator(path)) {
		const auto& path = dirEntry.path();
		std::ifstream tachiPlayer(path.string());

		Player player;
		player.supplement = supplement + path.stem().string();

		std::getline(tachiPlayer, nut);
		if (!nut.empty() && nut.back() == '\r') // on Linux getline splits on \n but files may be \r\n
			nut.pop_back();
		player.name = nut;

		while (std::getline(tachiPlayer, nut)) {
			if (!nut.empty() && nut.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				nut.pop_back();
			try {
				ss.clear();
				ss << nut;
				line.clear();
				md5.clear();
				std::getline(ss, line, ',');
				md5 = line;
				std::getline(ss, line, ',');
				int cleartype = from_chars<int>(line).value();
				if (!songTable.contains(md5)) continue;
				player.clears.insert_or_assign(md5, cleartype);
			}
			catch (const std::exception& e) {
				std::cout << "Parsing tachi player failed: " << std::quoted(nut) << ": " << e.what() << '\n';
				continue;
			}
		}
		playerEstimator(player);
		tachiPlayerTable.emplace(player.supplement, player);
		players << player.rating << ";" << adjRating((player.rating + summer) * scaler, &folderNormalizer) << ";" << player.supplement << ";" << player.name << '\n';
		if (std::ranges::contains(bokutachiplayers, player.supplement)) {
			writePlayerData(player, true);
		}
	}
}

static void recommend(int id, const std::vector<std::string>& ignores) {
	auto p_it = playerTable.find(id);
	if (p_it == playerTable.end()) return;
	Player& player = p_it->second;

	std::error_code ec;
	std::filesystem::create_directories(mode == 1 ? "output/sp/recommend" : "output/dp/recommend", ec);
	if (ec) {
		std::cout << "failed to create directory: " << ec.message() << '\n';
		return;
	}

	std::ofstream recommend((mode == 1) ? ("output/sp/recommend/" + std::to_string(id) + ".csv") : ("output/dp/recommend/" + std::to_string(id) + ".csv"));
	if (!recommend.is_open()) {
		std::cout << "!recommend.is_open()\n";
		return;
	}
	recommend << "md5;song;rating;adjRating;probability;cleartype\n";
	for (const auto& [sid, chart] : songTable) {
               if (std::ranges::any_of(ignores, [&](const std::string& i) { return chart.tablesFolders.contains(i); }))
                       continue;
		float ep = clearProbability(player.rating, chart.rating);
		float hp = clearProbability(player.rating, chart.hcrating);
		int cleartype = 0;
		if (auto it = std::ranges::find(chart.scores, id, &std::pair<int, int>::second); it != std::ranges::end(chart.scores))
			cleartype = it->second;
		switch (cleartype) {
		case 0:
			recommend << sid << ";" << chart.name << ";" << chart.rating << ";" << adjRating((chart.rating + summer) * scaler, &folderNormalizer) << ";" << ep << ";EASY\n";
			recommend << sid << ";" << chart.name << ";" << chart.hcrating << ";" << adjRating((chart.hcrating + summer) * scaler, &folderNormalizer) << ";" << hp << ";HARD\n";
			break;
		case 1:
			recommend << sid << ";" << chart.name << ";" << chart.hcrating << ";" << adjRating((chart.hcrating + summer) * scaler, &folderNormalizer) << ";" << hp << ";HARD\n";
			break;
		default:
			break;
		}
	}
}

static void recommendTachi(const std::string& id, const std::vector<std::string>& ignores) {
	auto p_it = tachiPlayerTable.find(id);
	if (p_it == tachiPlayerTable.end()) return;
	const Player& player = p_it->second;

	std::error_code ec;
	std::filesystem::create_directories(mode == 1 ? "output/sp/recommend" : "output/dp/recommend", ec);
	if (ec) {
		std::cout << "failed to create directory: " << ec.message() << '\n';
		return;
	}

	std::ofstream recommend((mode == 1) ? ("output/sp/recommend/" + id + ".csv") : ("output/dp/recommend/" + id + ".csv"));
	if (!recommend.is_open()) {
		std::cout << "!recommend.is_open()\n";
		return;
	}
	recommend << "md5;song;rating;adjRating;probability;cleartype\n";
	for (const auto& [sid, chart] : songTable) {
               if (std::ranges::any_of(ignores, [&](const std::string& i) { return chart.tablesFolders.contains(i); }))
                       continue;
		float ep = clearProbability(player.rating, chart.rating);
		float hp = clearProbability(player.rating, chart.hcrating);
		int cleartype = 0;
		if (auto it = player.clears.find(sid); it != player.clears.end())
			cleartype = it->second;
		switch (cleartype) {
		case 0:
			recommend << sid << ";" << chart.name << ";" << chart.rating << ";" << adjRating((chart.rating + summer) * scaler, &folderNormalizer) << ";" << ep << ";EASY\n";
			recommend << sid << ";" << chart.name << ";" << chart.hcrating << ";" << adjRating((chart.hcrating + summer) * scaler, &folderNormalizer) << ";" << hp << ";HARD\n";
			break;
		case 1:
			recommend << sid << ";" << chart.name << ";" << chart.hcrating << ";" << adjRating((chart.hcrating + summer) * scaler, &folderNormalizer) << ";" << hp << ";HARD\n";
			break;
		default:
			break;
		}
	}
}

int main(int argc, char** argv)
{
	constexpr auto&& usage = "{sp/dp} {table_to_ignore..}";
	if (argc < 2)
	{
		std::cout << usage << '\n';
		return 1;
	}

	bool enable_v2_data = false;
	if (std::string_view moder{argv[1]}; moder == "sp")
		mode = 1;
	else if (moder == "dp")
		mode = 2;
	else if (moder == "spv2")
		mode = 1, enable_v2_data = true;
	else if (moder == "dpv2")
		mode = 2, enable_v2_data = true;
	else
		return std::cout << usage << '\n', 1;
	std::cout << "mode: " << mode << '\n';

        std::vector<std::string> ignores;
	ignores.reserve(argc - 2);
	for (const char* table : std::span{argv, static_cast<std::size_t>(argc)}.subspan(2))
		ignores.emplace_back(table);

	std::cout << "the ignores are: ";
	for (std::string_view ignore : ignores) {
		std::cout << std::quoted(ignore) << " ";
	}
	std::cout << '\n';

	if (runFullIterations(enable_v2_data)) {
		std::cout << "runFullIterations failed\n";
		return 1;
	}
	calcOtherIRScores((mode == 1) ? "input/tachi7K" : "input/tachi14K", "t");

	for (int lr2id : lr2irplayers) {
		recommend(lr2id, ignores);
	}
	for (const char* tachiid : bokutachiplayers) {
		recommendTachi(tachiid, ignores);
	}
}
