#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <print>
#include <queue>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <cpr/cpr.h>
#include <iconv.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <sqlite3.h>

using namespace std::string_view_literals;

static constexpr int notify_every_this_fetches = 200;
static constexpr int requests_per_second = 20; // RPS
static constexpr int max_attempts = 5;
static constexpr auto&& begin_sql = "BEGIN TRANSACTION;";
static constexpr auto&& commit_sql = "COMMIT;";

#ifdef __linux__
#include <signal.h>

#include <sys/prctl.h>
[[nodiscard]] static std::string ellipsize(const std::string_view s, const size_t len)
{
    std::string out = std::format("{}~{}", s.substr(0, len / 2), s.substr(s.size() - len / 2));
    assert(out.size() <= len);
    return out;
}
static void SetThreadName(const char* name) // NOLINT(misc-no-recursion)
{
    // > The  name  can  be up to 16 bytes long, including the terminating null byte.
    static constexpr size_t max_name_len{15};
    std::string_view name_view{name};
    if (name_view.size() > max_name_len)
    {
        const std::string name_ = ellipsize(name_view, max_name_len);
        SetThreadName(name_.c_str());
        return;
    }
    assert(name_view.size() <= max_name_len);
    int ret = prctl(PR_SET_NAME, name);
    if (ret != 0)
    {
        // LOG_ERROR << "PR_SET_NAME failed, ret=" << ret;
    }
}
#elifdef _WIN32
static void SetThreadName(const char* name)
{
    (void)name;
}
#endif

struct ErrorDescription
{
    std::string msg;
};

static std::expected<void, ErrorDescription> convert(std::span<char> in_buf, std::string& output)
{
    struct IcdDeleter
    {
        void operator()(iconv_t icd)
        {
            if (reinterpret_cast<intptr_t>(icd) == -1)
                return;
            int ret = iconv_close(icd);
            if (ret == -1)
            {
                const int error = errno;
                std::println("iconv_close() error: {} ({})",
                             std::generic_category().default_error_condition(error).message(), error);
            }
        }
    };
    using IcdPtr = std::unique_ptr<std::remove_pointer_t<iconv_t>, IcdDeleter>;

    auto icd = IcdPtr(iconv_open("utf-8", "cp932"));
    if (reinterpret_cast<intptr_t>(icd.get()) == -1)
        if (int error = errno)
            return std::unexpected{
                ErrorDescription{std::format("iconv_open() error: {} ({})",
                                             std::generic_category().default_error_condition(error).message(), error)}};

    output.clear();
    char* src_ptr = in_buf.data();
    std::size_t src_size = in_buf.size();

    char buf[1024] /*[[indeterminate]]*/;
    while (src_size > 0)
    {
        char* dst_ptr = &buf[0];
        std::size_t dst_size = std::size(buf);
        std::size_t res = ::iconv(icd.get(), &src_ptr, &src_size, &dst_ptr, &dst_size);
        if (res == static_cast<std::size_t>(-1))
            if (int error = errno; error != E2BIG)
                return std::unexpected{ErrorDescription{
                    std::format("iconv() error: {} ({})",
                                std::generic_category().default_error_condition(error).message(), error)}};
        output.append(&buf[0], std::size(buf) - dst_size);
    }

    // > In each series of calls to iconv(), the last should be one with inbuf or *inbuf  equal  to NULL, in order to
    // > flush out any partially converted input.
    std::size_t res = ::iconv(icd.get(), nullptr, nullptr, nullptr, nullptr);
    if (res == static_cast<std::size_t>(-1))
        if (int error = errno)
            return std::unexpected{ErrorDescription{std::format(
                "iconv() error: {} ({})", std::generic_category().default_error_condition(error).message(), error)}};

    return {};
}

struct Md5
{
    constexpr Md5() { std::ranges::fill(data, 0); }
    constexpr explicit Md5(std::string_view s)
    {
        if (s.size() != sizeof(data))
            throw std::runtime_error("bad md5 size");
        std::ranges::copy(s, static_cast<char*>(data));
    }
    [[nodiscard]] constexpr std::string_view as_string_view() const { return {data, sizeof(data)}; }
    char data[32];
};

struct Score
{
    std::string player_name;
    int lr2id;
    // NoPlay = 0x0,
    // Fail = 0x1,
    // Easy = 0x2,
    // Groove = 0x3,
    // /// Good-attack plays also have this type of clear (unless they are also an FC).
    // Hard = 0x4,
    // FullCombo = 0x5,
    // // NOTE: there is no `PA` clear type.
    int clear;
    int combo;
    int minbp;
    // totalnotes from score.cgi
    int notes;
    int pgreat;
    int great;
};

struct GoodResult
{
    std::vector<Score> scores;
};

template <typename T> static std::optional<T> from_chars(std::string_view s)
{
    T out;
    if (auto ec = std::from_chars(s.data(), s.data() + s.size(), out); ec.ec != std::errc{})
        return std::nullopt;
    return {out};
}

[[nodiscard]] static std::string get_xml_string(xmlNodePtr node)
{
    auto* c = xmlNodeGetContent(node);
    // SAFETY: casting to char is always safe
    std::string s = reinterpret_cast<const char*>(c);
    xmlFree(c);
    return s;
}

[[nodiscard]] static std::expected<GoodResult, ErrorDescription> parse(std::span<char> data)
{
    if (data.empty())
        return std::unexpected{ErrorDescription{"empty data"}};

    data = data.subspan(1);

    if (auto pos = std::string_view{data}.find("<lastupdate>"); pos != std::string_view::npos)
        data = data.subspan(0, pos);
    else
        return std::unexpected{ErrorDescription{"<lastupdate> not found"}};

    std::string s;
    {
        std::vector<char> copy{data.begin(), data.end()};
        auto res = convert(copy, s);
        if (!res.has_value())
            return std::unexpected{ErrorDescription{res.error()}};
    }
    // NOTE: doesn't convert encoding correctly so we do it ourselves.
    auto doc = xmlReadMemory(s.data(), static_cast<int>(s.size()), nullptr, "utf-8", 0);
    if (doc == nullptr)
        return std::unexpected{ErrorDescription{"failed to parse XML"}};

    auto out = GoodResult{{}};

    auto* root = xmlDocGetRootElement(doc);
    if (root == nullptr)
        return std::unexpected{ErrorDescription{"xmlDocGetRootElement failed"}};

    for (auto* score_node = root->children; score_node; score_node = score_node->next)
    {
        if (score_node->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(score_node->name, BAD_CAST "score") != 0)
            return std::unexpected{ErrorDescription{"not a score node found"}};

        auto& score = out.scores.emplace_back();
        for (auto* score_field = score_node->children; score_field; score_field = score_field->next)
        {
            if (score_field->type != XML_ELEMENT_NODE)
                continue;

            auto read_and_assign_if_node_matches = [](xmlNodePtr node, const char* s,
                                                      int& out) -> std::expected<bool, ErrorDescription> {
                if (xmlStrcmp(node->name, BAD_CAST s) != 0)
                    return false;
                auto node_text = get_xml_string(node);
                auto v = from_chars<int>(node_text);
                if (!v)
                    return std::unexpected{ErrorDescription{std::format("invalid '{}': {}", s, node_text)}};
                out = *v;
                return true;
            };

            if (xmlStrcmp(score_field->name, BAD_CAST "name") == 0)
                score.player_name = get_xml_string(score_field);
            else
                for (auto [name, out] : std::to_array<std::pair<const char*, int&>>({
                         {"clear", score.clear},
                         {"id", score.lr2id},
                         {"combo", score.combo},
                         {"minbp", score.minbp},
                         {"notes", score.notes},
                         {"pg", score.pgreat},
                         {"gr", score.great},
                     }))
                {
                    auto res = read_and_assign_if_node_matches(score_field, name, out);
                    if (!res.has_value())
                        return std::unexpected{res.error()};
                    if (res.value())
                        break;
                }
        }

        // basic check that we've found required fields
        if (score.lr2id == 0)
            return std::unexpected{ErrorDescription{"'id' not found"}};
    }

    xmlFreeDoc(doc);

    return out;
}

[[nodiscard]] static std::expected<std::string, ErrorDescription> fetch(int your_lr2id, Md5 songmd5)
{
    cpr::Response r = cpr::Get(cpr::Url{std::format("http://www.dream-pro.info/~lavalse/LR2IR/2/"
                                                    "getrankingxml.cgi?id={}&songmd5={}&lastupdate=",
                                                    your_lr2id, songmd5.as_string_view())},
                               cpr::Timeout{std::chrono::seconds(15)},
                               cpr::UserAgent{"goodrating-lr2irscraper-contact-in-query-id/0.1"});
    if (r.status_code != 200)
        return std::unexpected{ErrorDescription{"status code != 200"}};
    return std::move(r.text);
}

template <class F> struct Defer
{
    Defer(F f) : f(f) {}
    ~Defer() { f(); }
    F f;
};
template <class F> [[nodiscard]] static Defer<F> mk_defer(F f)
{
    return Defer<F>(f);
};

[[nodiscard]] static std::expected<void, ErrorDescription> insert_scrapes(sqlite3* db, Md5 md5, const GoodResult& val,
                                                                          int64_t request_time)
{
    constexpr auto insert_scrape_sql =
        "INSERT INTO scrapes(md5, lr2id, lamp, combo, minbp, notes, pgreat, great, fetch_timestamp) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
        "ON CONFLICT(md5, lr2id) DO UPDATE "
        "SET md5 = ?1, lr2id = ?2, lamp = ?3, combo = ?4, minbp = ?5, notes = ?6, pgreat = ?7, great = ?8, fetch_timestamp = ?9"sv;

    sqlite3_exec(db, begin_sql, nullptr, nullptr, nullptr);
    auto _commit_sql = mk_defer([db]() { sqlite3_exec(db, commit_sql, nullptr, nullptr, nullptr); });

    sqlite3_stmt* stmt = nullptr;
    auto _bb_stmt = mk_defer([&stmt]() { sqlite3_finalize(stmt); });
    int ret = sqlite3_prepare_v3(db, insert_scrape_sql.data(), insert_scrape_sql.size(), 0, &stmt, nullptr);
    if (ret != 0)
        return std::unexpected{ErrorDescription{std::format("sqlite3_prepare_v3: {} ({})", sqlite3_errmsg(db), ret)}};
    sqlite3_bind_text(stmt, 1, md5.as_string_view().data(), md5.as_string_view().size(), nullptr);
    sqlite3_bind_int64(stmt, 9, request_time);
    for (const auto& score : val.scores)
    {
        sqlite3_bind_int(stmt, 2, score.lr2id);
        sqlite3_bind_int(stmt, 3, score.clear);
        sqlite3_bind_int(stmt, 4, score.combo);
        sqlite3_bind_int(stmt, 5, score.minbp);
        sqlite3_bind_int(stmt, 6, score.notes);
        sqlite3_bind_int(stmt, 7, score.pgreat);
        sqlite3_bind_int(stmt, 8, score.great);
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE)
            return std::unexpected{ErrorDescription{std::format("sqlite3_step: {} ({})", sqlite3_errmsg(db), ret)}};
        ret = sqlite3_reset(stmt);
        if (ret != SQLITE_OK)
            return std::unexpected{ErrorDescription{std::format("sqlite3_reset: {} ({})", sqlite3_errmsg(db), ret)}};
    }
    ret = sqlite3_finalize(stmt);
    stmt = nullptr;
    if (ret != SQLITE_OK)
        return std::unexpected{ErrorDescription{std::format("sqlite3_finalize: {} ({})", sqlite3_errmsg(db), ret)}};
    return {};
}

[[nodiscard]] static std::expected<void, ErrorDescription> insert_nicknames(sqlite3* db, const GoodResult& val,
                                                                            int64_t request_time)
{
    constexpr auto insert_nickname_sql = "INSERT INTO nicknames(lr2id, name, fetch_timestamp) VALUES (?1, ?2, ?3) "
                                         "ON CONFLICT(lr2id) DO UPDATE "
                                         "SET lr2id = ?1, name = ?2, fetch_timestamp = ?3"sv;

    sqlite3_exec(db, begin_sql, nullptr, nullptr, nullptr);
    auto _commit_sql = mk_defer([db]() { sqlite3_exec(db, commit_sql, nullptr, nullptr, nullptr); });

    sqlite3_stmt* stmt = nullptr;
    auto _bb_stmt = mk_defer([&stmt]() { sqlite3_finalize(stmt); });
    int ret = sqlite3_prepare_v3(db, insert_nickname_sql.data(), insert_nickname_sql.size(), 0, &stmt, nullptr);
    if (ret != 0)
        return std::unexpected{ErrorDescription{std::format("sqlite3_prepare_v3: {} ({})", sqlite3_errmsg(db), ret)}};
    sqlite3_bind_int64(stmt, 3, request_time);
    for (const auto& score : val.scores)
    {
        sqlite3_bind_int(stmt, 1, score.lr2id);
        sqlite3_bind_text(stmt, 2, score.player_name.data(), static_cast<int>(score.player_name.size()), nullptr);
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE)
            return std::unexpected{ErrorDescription{std::format("sqlite3_step: {} ({})", sqlite3_errmsg(db), ret)}};
        ret = sqlite3_reset(stmt);
        if (ret != SQLITE_OK)
            return std::unexpected{ErrorDescription{std::format("sqlite3_reset: {} ({})", sqlite3_errmsg(db), ret)}};
    }
    ret = sqlite3_finalize(stmt);
    stmt = nullptr;
    if (ret != SQLITE_OK)
        return std::unexpected{ErrorDescription{std::format("sqlite3_finalize: {} ({})", sqlite3_errmsg(db), ret)}};
    return {};
}

[[nodiscard]] static std::expected<void, ErrorDescription> insert_to_retry(sqlite3* db, Md5 md5, std::string_view error)
{
    constexpr auto insert_to_retry_sql = "INSERT INTO to_retry(md5, reason) VALUES (?1, ?2) "
                                         "ON CONFLICT(md5) DO UPDATE "
                                         "SET md5 = ?1, reason = ?2"sv;

    sqlite3_exec(db, begin_sql, nullptr, nullptr, nullptr);
    auto _commit_sql = mk_defer([db]() { sqlite3_exec(db, commit_sql, nullptr, nullptr, nullptr); });

    sqlite3_stmt* stmt = nullptr;
    auto _bb_stmt = mk_defer([&stmt]() { sqlite3_finalize(stmt); });
    int ret = sqlite3_prepare_v3(db, insert_to_retry_sql.data(), insert_to_retry_sql.size(), 0, &stmt, nullptr);
    if (ret != 0)
        return std::unexpected{
            ErrorDescription{std::format("sqlite3_prepare_v3 to_retry: {} ({})", sqlite3_errmsg(db), ret)}};
    sqlite3_bind_text(stmt, 1, md5.as_string_view().data(), md5.as_string_view().size(), nullptr);
    sqlite3_bind_text(stmt, 2, error.data(), static_cast<int>(error.size()), nullptr);
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE)
        return std::unexpected{ErrorDescription{std::format("sqlite3_step: {} ({})", sqlite3_errmsg(db), ret)}};
    ret = sqlite3_finalize(stmt);
    stmt = nullptr;
    if (ret != SQLITE_OK)
        return std::unexpected{ErrorDescription{std::format("sqlite3_finalize: {} ({})", sqlite3_errmsg(db), ret)}};
    return {};
}

struct SqliteDeleter
{
    void operator()(sqlite3* db) { sqlite3_close(db); }
};
using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;

struct WorkPool
{
    std::queue<Md5> queue;
    std::mutex queue_mutex;
    std::vector<std::tuple<Md5, GoodResult, int64_t>> out_queue;
    std::vector<std::pair<Md5, std::string>> out_error_queue;
    std::mutex out_queues_mutex;
    std::atomic<size_t> total_fetched;
    std::atomic<size_t> workers_alive;
    int your_lr2id;
    // No more work is coming, workers should stop once they finish their work
    std::atomic<bool> should_stop;
    // We want to quit, workers should stop ASAP
    std::atomic<bool> force_stop;
};

static void work(WorkPool* work_pool_, const unsigned worker_count)
{
    auto& work_pool = *work_pool_;
    const size_t worker = work_pool.workers_alive++;
    auto _bye_worker = mk_defer([&work_pool]() { work_pool.workers_alive--; });
    SetThreadName(std::format("worker {}", worker).c_str());

    std::mt19937 jitter_gen{std::random_device{}()};

    std::vector<std::pair<std::string, int64_t>> errors;
    while (!work_pool.should_stop && !work_pool.force_stop)
    {
        Md5 md5_to_fetch;
        {
            std::unique_lock lock{work_pool.queue_mutex};
            if (work_pool.queue.empty())
            {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            md5_to_fetch = work_pool.queue.front();
            work_pool.queue.pop();
        }

        errors.clear();
        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            const auto request_time = std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::high_resolution_clock::now().time_since_epoch())
                                          .count();
            auto val =
                fetch(work_pool.your_lr2id, md5_to_fetch).and_then([](std::string&& s) { return parse(std::span{s}); });
            if (val)
            {
                std::unique_lock lock{work_pool.out_queues_mutex};
                work_pool.out_queue.emplace_back(md5_to_fetch, std::move(val->scores), request_time);
            }
            else
            {
                errors.emplace_back(std::move(val.error().msg), request_time);
            }

            if (work_pool.force_stop) // after finishing work
                break;

            // simplest rate limiting ever. kinda bursty but who cares.
            constexpr int max_jitter = 500;
            std::this_thread::sleep_for(
                (std::chrono::milliseconds(1000) / requests_per_second * worker_count) +
                std::chrono::milliseconds(std::uniform_int_distribution{0, max_jitter}(jitter_gen)));

            if (val)
                break;
        }

        auto concat_errors = [](std::span<const std::pair<std::string, int64_t>> errors) {
            std::string out;
            for (const auto& [error, timestamp] : errors)
                std::format_to(std::back_inserter(out), "[{}]{}|", timestamp, error);
            if (!out.empty())
                out.pop_back();
            return out;
        };

        if (auto error = concat_errors(errors); !error.empty())
        {
            std::unique_lock lock{work_pool.out_queues_mutex};
            work_pool.out_error_queue.emplace_back(md5_to_fetch, std::move(error));
        }

        auto fetched = ++work_pool.total_fetched;
        if (fetched % notify_every_this_fetches == 0)
        {
            const size_t count = [&work_pool]() {
                std::lock_guard lock{work_pool.queue_mutex};
                return work_pool.queue.size();
            }();
            // even jitter is not included not to mention this should use actual request count so the value is
            // scuffed
            const auto seconds_left = count / requests_per_second;
            std::println("fetched scores for {} charts so far... ({} left ~{:02}:{:02})", fetched, count,
                         seconds_left / 60, seconds_left % 60);
        }
    }
};

static void work_sql(WorkPool* work_pool_, sqlite3* db)
{
    auto& work_pool = *work_pool_;
    std::vector<std::tuple<Md5, GoodResult, int64_t>> vals;
    std::vector<std::pair<Md5, std::string>> errors;
    // no 'should_stop', fetch workers may still want to insert some data
    while (!work_pool.force_stop)
    {
        {
            std::unique_lock lock{work_pool.out_queues_mutex};
            vals.insert(vals.end(), std::make_move_iterator(work_pool.out_queue.begin()),
                        std::make_move_iterator(work_pool.out_queue.end()));
            work_pool.out_queue.clear();
            errors.insert(errors.end(), std::make_move_iterator(work_pool.out_error_queue.begin()),
                          std::make_move_iterator(work_pool.out_error_queue.end()));
            work_pool.out_error_queue.clear();
        }

        if (vals.empty() && errors.empty())
        {
            if (work_pool.workers_alive == 0)
                break; // done
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        for (auto&& [md5, error] : errors)
        {
            if (auto ok = insert_to_retry(db, md5, error); !ok)
            {
                std::println("insert_to_retry: {}", ok.error().msg);
                work_pool.force_stop = true;
            }
        }
        errors.clear();

        for (auto&& [md5, val, request_time] : vals)
        {
            if (auto ok = insert_scrapes(db, md5, val, request_time); !ok)
            {
                std::println("insert_scrapes: ", ok.error().msg, request_time);
                work_pool.force_stop = true;
            }
            if (auto ok = insert_nicknames(db, val, request_time); !ok)
            {
                std::println("insert_nicknames: ", ok.error().msg, request_time);
                work_pool.force_stop = true;
            }
        }
        vals.clear();
    }
};

static std::expected<void, ErrorDescription> mass_insert_to_retry(WorkPool& work_pool, sqlite3* db)
{
    constexpr auto insert_to_retry_sql = "INSERT INTO to_retry(md5, reason) VALUES (?1, ?2) "
                                         "ON CONFLICT(md5) DO UPDATE "
                                         "SET md5 = ?1, reason = ?2"sv;

    sqlite3_exec(db, begin_sql, nullptr, nullptr, nullptr);
    auto _commit_sql = mk_defer([db]() { sqlite3_exec(db, commit_sql, nullptr, nullptr, nullptr); });

    constexpr auto error = "early-exit"sv;
    sqlite3_stmt* stmt = nullptr;
    auto _bb_stmt = mk_defer([&stmt]() { sqlite3_finalize(stmt); });
    int ret = sqlite3_prepare_v3(db, insert_to_retry_sql.data(), insert_to_retry_sql.size(), 0, &stmt, nullptr);
    if (ret != 0)
        return std::unexpected{ErrorDescription{std::format("sqlite3_prepare_v3: {} ({})", sqlite3_errmsg(db), ret)}};
    if (ret != 0)
        return std::unexpected{ErrorDescription{std::format("sqlite3_prepare_v3: {} ({})", sqlite3_errmsg(db), ret)}};
    while (!work_pool.queue.empty())
    {
        const auto& task = work_pool.queue.front();

        sqlite3_bind_text(stmt, 1, task.as_string_view().data(), task.as_string_view().size(), nullptr);
        sqlite3_bind_text(stmt, 2, error.data(), error.size(), nullptr);
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE)
            return std::unexpected{ErrorDescription{std::format("sqlite3_step: {} ({})", sqlite3_errmsg(db), ret)}};
        ret = sqlite3_reset(stmt);
        if (ret != SQLITE_OK)
            return std::unexpected{ErrorDescription{std::format("sqlite3_reset: {} ({})", sqlite3_errmsg(db), ret)}};

        work_pool.queue.pop();
    }
    ret = sqlite3_finalize(stmt);
    stmt = nullptr;
    if (ret != SQLITE_OK)
        return std::unexpected{ErrorDescription{std::format("sqlite3_finalize: {} ({})", sqlite3_errmsg(db), ret)}};
    return {};
};

static std::expected<int64_t, ErrorDescription> select_to_retry_count(sqlite3* db)
{
    constexpr auto select_to_retry_count_sql = "SELECT count(1) FROM to_retry"sv;
    sqlite3_stmt* stmt = nullptr;
    auto _bb_stmt = mk_defer([&stmt]() { sqlite3_finalize(stmt); });
    int ret =
        sqlite3_prepare_v3(db, select_to_retry_count_sql.data(), select_to_retry_count_sql.size(), 0, &stmt, nullptr);
    if (ret != 0)
        return std::unexpected{ErrorDescription{std::format("sqlite3_prepare_v3: {} ({})", sqlite3_errmsg(db), ret)}};
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW)
        return std::unexpected{ErrorDescription{std::format("sqlite error: {} ({})", sqlite3_errmsg(db), ret)}};
    const int64_t out = sqlite3_column_int64(stmt, 0);
    ret = sqlite3_finalize(stmt);
    stmt = nullptr;
    if (ret != SQLITE_OK)
        return std::unexpected{ErrorDescription{std::format("sqlite3_finalize: {} ({})", sqlite3_errmsg(db), ret)}};
    return out;
};

static std::atomic<bool> interrupted;
#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl)
{
    if (ctrl == CTRL_C_EVENT)
        interrupted = true;
    return TRUE;
}
#endif // _WIN32

int main(int argc, char* argv[]) // NOLINT(bugprone-exception-escape)
{
    constexpr auto&& usage = "lr2irscraper {result.db} {your_lr2id} {?md5list.txt}\n"
                             "  md5list.txt - don't pass to continue from where stopped";

    if (sqlite3_threadsafe() == 0)
    {
        std::println("sqlite3_threadsafe() == 0");
        return 1;
    }

    std::span args{argv, static_cast<size_t>(argc)};
    if (args.size() != 3 && args.size() != 4)
    {
        std::println("{}", usage);
        return 1;
    }
    const char* db_path = args[1];
    int your_lr2id;
    if (auto v = from_chars<int>(args[2]))
        your_lr2id = *v;
    else
        return std::println("failed to parse 'your_lr2id'"), 1;
    // LR2IR returns empty answers for such LR2IDs
    if (your_lr2id > 999'999)
        return std::println("invalid 'your_lr2id': above 999999: {}", your_lr2id), 1;
    const char* md5list_path = args.size() == 4 ? args[3] : nullptr;

    WorkPool work_pool;
    work_pool.your_lr2id = your_lr2id;

    SqlitePtr db;
    {
        sqlite3* db_;
        int ret = sqlite3_open(db_path, &db_);
        db.reset(db_);
        if (ret != SQLITE_OK)
        {
            std::println("sqlite3_open: {} ({})", sqlite3_errmsg(db.get()), ret);
            return 1;
        }
    }

    int ret = sqlite3_exec(db.get(), R"(
CREATE TABLE IF NOT EXISTS scrapes(
  md5 VARCHAR(32) NOT NULL,
  lr2id INTEGER NOT NULL,
  lamp INTEGER NOT NULL,
  combo INTEGER NOT NULL,
  minbp INTEGER NOT NULL,
  notes INTEGER NOT NULL,
  pgreat INTEGER NOT NULL,
  great INTEGER NOT NULL,
  fetch_timestamp BIGINT NOT NULL,
  UNIQUE(md5, lr2id)
);
CREATE TABLE IF NOT EXISTS nicknames(
  lr2id INTEGER NOT NULL,
  name VARCHAR NOT NULL,
  fetch_timestamp BIGINT NOT NULL,
  UNIQUE(lr2id)
);
CREATE TABLE IF NOT EXISTS to_retry(
  md5 VARCHAR(32) NOT NULL,
  reason VARCHAR NOT NULL,
  UNIQUE(md5)
);

-- PC dies = RIP data. Improves performance 50x.
PRAGMA synchronous=OFF;
	)",
                           nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK)
    {
        std::println("sqlite3_exec: {} ({})", sqlite3_errmsg(db.get()), ret);
        return 1;
    }

    static constexpr unsigned worker_cap = 16u;

    std::vector<std::jthread> workers;
    {
        const auto worker_count = std::min(std::max(std::thread::hardware_concurrency(), 2u), worker_cap);
        workers.reserve(worker_count);
        std::println("spawning {} workers", worker_count);
        for (auto i = 0u; i < worker_count; ++i)
            workers.emplace_back(work, &work_pool, worker_count);
    }

    if (md5list_path == nullptr)
    {
        // Continue previous fetching
        constexpr auto select_from_retry_sql = "SELECT md5 FROM to_retry"sv;
        sqlite3_stmt* stmt = nullptr;
        auto _bb_stmt = mk_defer([&stmt]() { sqlite3_finalize(stmt); });
        int ret =
            sqlite3_prepare_v3(db.get(), select_from_retry_sql.data(), select_from_retry_sql.size(), 0, &stmt, nullptr);
        if (ret != 0)
        {
            std::println("sqlite3_prepare_v3: {} ({})", sqlite3_errmsg(db.get()), ret);
            work_pool.force_stop = true;
        }

        size_t song_count = 0;
        while (true)
        {
            ret = sqlite3_step(stmt);
            if (ret == SQLITE_DONE)
                break;
            if (ret != SQLITE_ROW)
            {
                work_pool.force_stop = true;
                std::println("sqlite error: {} ({})", sqlite3_errmsg(db.get()), ret);
                break;
            }
            // reinterpret_cast SAFETY: casting to char is always safe
            auto text = std::string_view{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                                         static_cast<size_t>(sqlite3_column_bytes(stmt, 0))};
            if (text.size() != 32)
            {
                work_pool.force_stop = true;
                std::println("invalid md5 length in db: {}", text);
                break;
            }

            song_count++;
            std::lock_guard lock{work_pool.queue_mutex};
            work_pool.queue.emplace(text);
        }

        ret = sqlite3_finalize(stmt);
        stmt = nullptr;
        if (ret != SQLITE_OK)
        {
            std::println("sqlite3_finalize: {} ({})", sqlite3_errmsg(db.get()), ret);
            work_pool.force_stop = true;
        }

        // NOTE: we DELETE separately after instead of DELETE FROM as that would delete unhandled rows on errors.
        if (!work_pool.force_stop)
        {
            ret = sqlite3_exec(db.get(), "DELETE FROM to_retry;", nullptr, nullptr, nullptr);
            if (ret != SQLITE_OK)
            {
                std::println("sqlite3_exec: {} ({})", sqlite3_errmsg(db.get()), ret);
                work_pool.force_stop = true;
            }
        }

        if (!work_pool.force_stop)
            workers.emplace_back(work_sql, &work_pool, db.get());

        std::println("produced {} fetch tasks", song_count);
    }
    else
    {
        workers.emplace_back(work_sql, &work_pool, db.get());
        // Read tasks from supplied file
        if (std::ifstream ifs(md5list_path); ifs.is_open())
        {
            std::stringstream md5list_ss;
            md5list_ss << ifs.rdbuf();
            ifs.close();
            size_t song_count = 0;
            for (std::string line; std::getline(md5list_ss, line);)
            {
                // no header
                if (line.starts_with('#'))
                    continue;
                if (line.length() != 32)
                {
                    std::println("invalid md5 in md5list: {}", line);
                    work_pool.force_stop = true;
                    break;
                }
                work_pool.queue.emplace(line);
                song_count++;
            }
            std::println("produced {} fetch tasks", song_count);
        }
        else
        {
            work_pool.force_stop = true;
            std::println("failed to read supplied task file");
        }
    }
#ifdef __linux__
    struct sigaction sa;
    sa.sa_handler = [](int /*signum*/) { interrupted = true; };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, nullptr) == -1)
    {
        std::println("failed to set up SIGINT handler");
        work_pool.force_stop = true;
    }
#elifdef _WIN32
    SetConsoleCtrlHandler(&console_ctrl_handler, TRUE);
#endif // __linux__

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (interrupted || work_pool.force_stop)
            break;
        std::lock_guard lock{work_pool.queue_mutex};
        if (work_pool.queue.empty())
            break;
    }
    // ctrl-c won't work here to speed up the exit... not cool
    std::println("all tasks consumed, joining workers");
    work_pool.should_stop = true;
    workers.clear();

    // save charts to fetch in case of an early exit like Ctrl+C
    if (!work_pool.queue.empty())
        if (auto ok = mass_insert_to_retry(work_pool, db.get()); !ok)
            std::println("mass_insert_to_retry: {}", ok.error().msg);

    if (work_pool.force_stop)
        std::println("emergency landing! you should start scraping from scratch now :(");
    else if (auto ok = select_to_retry_count(db.get()); !ok)
        std::println("select_to_retry_count: {}", ok.error().msg);
    else if (*ok > 0)
        std::println("restart without supplying a list now, we need to retry fetching {} charts", *ok);
    else
        std::println("all done!");

    return 0;
}
