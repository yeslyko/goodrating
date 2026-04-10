#include <algorithm>
#include <atomic>
#include <charconv>
#include <format>
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
	int lr2id{};
	float rating{};
	std::unordered_map<std::string, int> clears; // md5, clear
	std::string supplement;
	// PERF: vector performs better than unordered_map on the amount of levels tables usually have.
	std::unordered_map<std::string, std::vector<std::pair<int, int>>> completionList;
};

struct Chart {
	std::string name;
	std::unordered_map<std::string, int> tablesFolders;
	float rating{};
	float hcrating{};
	std::vector<std::pair<int, int>> scores; // [lr2id][clearType]
	int playcount{};
	float cleardiffsd{};
};

static std::vector<int> lr2irplayers;
static std::vector<std::string> bokutachiplayers;

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

	for (auto & [md5, clear] : player.clears) {
		if (clear == 0) {
			continue;
		}
		const auto& chart = songTable.at(md5);
		clearRatings.push_back(clear == 2 ? chart.hcrating : chart.rating);
	}
	if (clearRatings.empty()) {
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
	if (normalizer->empty()) {
		return -999;
	}
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

	float min_ratings = 0;
	int min_count = 0;
	for (auto& [_, chart] : songTable) {
		if (auto it = chart.tablesFolders.find(normal);
				it != chart.tablesFolders.end() && it->second == 1) {
			min_ratings += chart.rating;
			min_count++;
		}
	}
	summer = min_ratings / static_cast<float>(min_count);

	float max_ratings = 0;
	int max_count = 0;
	for (auto& [_, chart] : songTable) {
		if (auto it = chart.tablesFolders.find(insane);
				it != chart.tablesFolders.end() && it->second == ((mode == 1) ? 25 : 13)) {
			max_ratings += chart.rating + summer;
			max_count++;
		}
	}
	scaler = ((mode == 1) ? 36.5F : 24.5F) / (max_ratings / static_cast<float>(max_count));
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
		for (auto& [_, chart] : songTable) {
			auto normal_it = chart.tablesFolders.find(normal);
			auto insane_it = chart.tablesFolders.find(insane);
			auto end = chart.tablesFolders.end();
			if (normal_it == end && insane_it == end) {
				continue;
			}
			int folder = [&]() {
				if (normal_it != chart.tablesFolders.end())
					return normal_it->second;
				return chart.tablesFolders.at(insane);
			}();
			if (normal_it != end && folder > 11) continue;
			if (insane_it != end) folder += 11;
			if (folder != i) {
				continue;
			}
			flotsam += (chart.rating + summer) * scaler;
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

static float guessRating(const Chart& chart) {
	const bool isCleared = std::ranges::any_of(chart.scores, std::identity{}, &std::pair<int, int>::second);
	if (isCleared) {
		float minRating = 999.F;
		for (auto [lr2id, clear] : chart.scores) {
			if (clear == 0) continue;
			minRating = std::min(playerTable.find(lr2id)->second.rating, minRating);
		}
		return minRating;
	}
	float maxRating = -999.F;
	for (auto [lr2id, _clear] : chart.scores) {
		maxRating = std::max(playerTable.find(lr2id)->second.rating, maxRating);
	}
	return maxRating;
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
	if (s.empty()) {
		return 0;
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
static std::string load_dataset(int mode, std::unordered_map<int, Player>& playerTable,
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

			auto fmt_error = [&](auto&& s) {
				return std::format("{}: {}: {}: {}: {}", line, md5, table, level, s);
			};

			auto chart = songTable.find(md5);
			if (chart == songTable.end()) {
				return fmt_error("chart table level for missing song");
			}
			auto levels_as_ints = table_level_as_int.find(table);
			if (levels_as_ints == table_level_as_int.end()) {
				return fmt_error("missing table for level as int");
			}
			auto level_as_int = levels_as_ints->second.find(level);
			if (levels_as_ints == table_level_as_int.end()) {
				return fmt_error("missing level as int");
			}
			auto level_ratings = table_level_rating.find(table);
			if (level_ratings == table_level_rating.end()) {
				return fmt_error("missing table for table level rating");
			}
			auto rating = level_ratings->second.find(level);
			if (rating == level_ratings->second.end()) {
				return fmt_error("missing table level rating");
			}

			chart->second.tablesFolders[table] = level_as_int->second;
			// std::max is dumb but at least makes the output independent of input data order
			// TODO: table priority?
			chart->second.rating = std::max(rating->second, chart->second.rating);
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

			int new_clear;
			switch (*lamp) {
				case 0: 		      // noplay
				case 1: new_clear = 0; break; // fail
				case 2: 		      // easy
				case 3: new_clear = 1; break; // groove
				case 4:      	              // hard
				case 5: new_clear = 2; break; // fc
				default: std::cout << line << ": bad lamp " << *lamp << ' ' << *lr2id << ' ' << md5 << '\n'; continue;
			}

			auto clear = player->second.clears.find(md5);
			if (clear == player->second.clears.end()) {
				player->second.clears.insert_or_assign(md5, new_clear);
				chart->second.scores.emplace_back(*lr2id, new_clear);
			} else {
				std::cout << "found score on " << md5 << " for " << *lr2id << " again " << clear->second <<" -> " <<new_clear <<" \n";
				clear->second = std::max(clear->second, new_clear);
				auto existing_score = std::ranges::find(chart->second.scores, *lr2id, &std::pair<int, int>::first);
				if (existing_score == std::ranges::end(chart->second.scores)) {
					return std::to_string(line) + ": PROGRAMMER ERROR: " + std::to_string(*lr2id) + " " + md5;
				}
				existing_score->second = clear->second;
			}
			chart->second.playcount++;
		}
	}

	if (parts_loaded != parts_total) {
		return "missing data, loaded " + std::to_string(parts_loaded) + '/' + std::to_string(parts_total) + " parts";
	}

	return {};
}

// \return non-empty string on error
static std::string loadPlayerList(int mode) {
	std::string line_buf;
	std::vector<std::string> csv_buf;

	if (auto ifs = std::ifstream{ mode == 1 ? "input/sp_playerlist_lr2ir.csv" : "input/dp_playerlist_lr2ir.csv" }; ifs.is_open()) {
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
			if (auto z = split_csv(csv_buf, line_buf); !z) {
				return std::format("{}: invalid csv field count in playerlist file: {} == 0", line, z);
			}
			if (!line_buf.empty() && line_buf.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				line_buf.pop_back();

			auto lr2id = from_chars<int>(csv_buf[0]);
			if (!lr2id) {
				return std::format("{}: invalid 'lr2id': {}", line, csv_buf[0]);
			}

			lr2irplayers.push_back(*lr2id);
		}
	}

	if (auto ifs = std::ifstream{ mode == 1 ? "input/sp_playerlist_tachi.csv" : "input/dp_playerlist_tachi.csv" }; ifs.is_open()) {
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
			if (auto z = split_csv(csv_buf, line_buf); z == 0) {
				return std::format("{}: invalid csv field count in playerlist file: {} == 0", line, z);
			}
			if (!line_buf.empty() && line_buf.back() == '\r') // on Linux getline splits on \n but files may be \r\n
				line_buf.pop_back();

			auto tachiid = from_chars<int>(csv_buf[0]);
			if (!tachiid) {
				return std::format("{}: invalid 'tachiid': {}", line, csv_buf[0]);
			}

			bokutachiplayers.push_back(std::format("t{}", *tachiid));
		}
	}

	return {};
}

static bool runFullIterations() {
	std::cout << "loading data..." << '\n';
	auto beg = std::chrono::high_resolution_clock::now();
	if(auto error = load_dataset(mode, playerTable, songTable); !error.empty()){
		std::cout << "loading data failed: " << error << '\n';
		return true;
	}
	std::cout << "loaded data in "
		<< std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - beg).count()
		<< " seconds\n";

	using Md5 = std::string;
	using Lr2id = int;
	std::unordered_map<Md5, std::unordered_map<Lr2id, float>> fail_weights;
	{
		std::cout << "precomputing data..." << '\n';
		auto beg = std::chrono::high_resolution_clock::now();
		countFolderCompletions();
		countChartCount();
		for (const auto& [md5, chart] : songTable) {
			for (const auto& [lr2id, _clear] : chart.scores) {
				fail_weights[md5][lr2id] = calcFailWeight(playerTable.at(lr2id), chart);
			}
		}
		std::cout << "precomputed data in "
			<< std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - beg).count()
			<< " seconds\n";
	}

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
	std::vector<std::pair<std::string, Chart*>> songPtrs;

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
			std::cout << "first run precalculations..." << '\n';
			auto beg = std::chrono::high_resolution_clock::now();
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
			songPtrs.reserve(songTable.size());
			for (auto& kv : songTable) {
				songPtrs.emplace_back(kv.first, &kv.second);
			}
			std::cout << "first run calculations ended in "
				<< std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - beg).count()
				<< " seconds\n";
		}

		//run ec ratings for each file
		calcTableAverages(tableAverages);

		std::atomic<float> localEcMean = 0;
#pragma omp parallel for
		// NOLINTNEXTLINE(modernize-loop-convert) openmp
		for (int i = 0; i < static_cast<int>(songPtrs.size()); ++i) {
			auto& [md5, chart_] = songPtrs[i];
			auto& chart = *chart_;
			float sum = 0.f;
			float cr = chart.rating;
			float totalRelevance = 1.F;
			float relevance = 0.F;

			for (auto & [lr2id, clear] : chart.scores) {
				const Player& player = playerTable.at(lr2id);
				float pr = player.rating;
				float failWeight = fail_weights.at(md5).at(lr2id);
				relevance = calcRelevance(pr, cr) * ((clear == 0) ? failWeight : 1);
				if ((pr < cr) && (clear > 0)) relevance += cr - pr;
				sum += scale * chartEstimator(cr, pr, clear, 0) * relevance;
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
		ecMean += localEcMean;

		totalCharts += songTable.size();
		ecMean /= static_cast<float>(totalCharts);
		ecSigma = 0;

		//run hc ratings for each file
		std::atomic<float> localEcSigma = 0;
#pragma omp parallel for
		// NOLINTNEXTLINE(modernize-loop-convert) openmp
		for (int i = 0; i < static_cast<int>(songPtrs.size()); ++i) {
			auto& [md5, chart_] = songPtrs[i];
			auto& chart = *chart_;
			float sum = 0.f;
			float cr = chart.hcrating;
			float clearsd = 0.f;
			int clearpc = 0;
			float totalRelevance = 1.F;
			float relevance = 0.F;

			for (auto & [lr2id, clear] : chart.scores) {
				const Player& player = playerTable.find(lr2id)->second;
				float pr = player.rating;
				float failWeight = fail_weights[md5][lr2id];
				relevance = calcRelevance(pr, cr) * ((clear < 2) ? failWeight : 1);
				if ((pr < cr) && (clear == 2)) relevance += cr - pr;
				sum += scale * chartEstimator(cr, pr, clear, 1) * relevance;
				totalRelevance += relevance;

				if ((((pr < cr) && (clear > 0)) || ((pr >= cr) && (clear == 0))) &&
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

		ecSigma += localEcSigma;
		iter--;

		std::cout << "(" << helper - iter << "/" << helper << ") iterations completed..." << '\n';
		std::cout << "x: " << ecMean << " - s: " << std::sqrt(ecSigma / static_cast<float>(totalCharts)) << " - " << ecMean - prevMean << ", " << std::sqrt(ecSigma / static_cast<float>(totalCharts)) - std::sqrt(prevSigma / static_cast<float>(totalCharts)) << '\n';
	}

	auto iterend = std::chrono::high_resolution_clock::now();
	std::cout << helper << " iterations completed in " << std::chrono::duration<double>{iterend - iterstart}.count()
		<< " seconds.\n";

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
	for (auto& [md5, chart] : songTable) {
		float adjEC = adjRating((chart.rating + summer) * scaler, &folderNormalizer);
		float adjHC = adjRating((chart.hcrating + summer) * scaler, &folderNormalizer);
		std::string folderCheck;
		if (chart.tablesFolders.begin()->second == -1) folderCheck = "?"; else folderCheck = std::to_string(chart.tablesFolders.begin()->second);
		CRTable << chart.tablesFolders.begin()->first << ";" << folderCheck << ";" <<
			chart.rating << ";" << chart.hcrating << ";" << adjEC << ";" << adjHC << ";" <<
			chart.cleardiffsd << ";" << chart.name << ";" << md5 << "\n";
	}
	CRTable.close();

	std::ofstream PRTable((mode == 1) ? "output/sp/players.csv" : "output/dp/players.csv");
	if (!PRTable.is_open()) {
		std::cout << "!PRTable.is_open()\n";
		return true;
	}
	PRTable << "rating;adjRating;lr2id;name\n";
	for (auto & [lr2id, player] : playerTable) {
		if (player.rating == -999) continue;
		float adjRate = adjRating((player.rating + summer) * scaler, &folderNormalizer);
		PRTable << player.rating << ";" << adjRate << ";" << lr2id << ";" << player.name << "\n";
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

	if (std::string_view moder{argv[1]}; moder == "sp")
		mode = 1;
	else if (moder == "dp")
		mode = 2;
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

	if (auto error = loadPlayerList(mode); !error.empty()) {
		std::cout << "loadPlayerList failed: " << error << "\n";
		return 1;
	}

	if (runFullIterations()) {
		std::cout << "runFullIterations failed\n";
		return 1;
	}
	calcOtherIRScores((mode == 1) ? "input/tachi7K" : "input/tachi14K", "t");

	for (int lr2id : lr2irplayers) {
		recommend(lr2id, ignores);
	}
	for (const std::string& tachiid : bokutachiplayers) {
		recommendTachi(tachiid, ignores);
	}
}
