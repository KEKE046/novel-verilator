#include "verilated.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>

namespace {

static size_t toggle_N = 32;

struct CovInfo {
    bool active = false;
    uint8_t value = -1;
};

struct FileDumper {
    FILE* fp_time;
    FILE* fp_indptr;
    FILE* fp_indices;
    FILE* fp_data;
    FileDumper(std::string prefix) {
        fp_time = fopen((prefix + ".time.bin").c_str(), "wb");
        fp_indptr = fopen((prefix + ".indptr.bin").c_str(), "wb");
        fp_indices = fopen((prefix + ".indices.bin").c_str(), "wb");
        fp_data = fopen((prefix + ".data.bin").c_str(), "wb");
    }
    ~FileDumper() {
        if (current_time != last_time) { sweep_active(); }
        sweep_last();
        fclose(fp_time);
        fclose(fp_indptr);
        fclose(fp_indices);
        fclose(fp_data);
    }
    bool first_submit = true;
    uint64_t last_time = -1;
    uint64_t current_time = 0;
    uint64_t current_index = 0;
    std::vector<CovInfo> current_value;
    std::vector<uint64_t> active_id;
    uint32_t log_counter = 0;
    uint64_t time_counter = 0;
    void sweep_active() {
        if (log_counter++ >= 1000) {
            std::cerr << "DUMPER: " << time_counter << " time=" << current_time
                      << " rate=" << std::fixed << std::setprecision(2)
                      << 1.0 * current_index / time_counter << std::endl;
            log_counter = 0;
        }
        time_counter++;
        // std::cout << "sweep active: " << current_time << std::endl;
        fwrite(&current_time, sizeof(current_time), 1, fp_time);
        fwrite(&current_index, sizeof(current_index), 1, fp_indptr);
        std::sort(active_id.begin(), active_id.end());
        for (auto id : active_id) {
            current_value[id].active = false;
            uint8_t value = current_value[id].value;
            fwrite(&id, sizeof(id), 1, fp_indices);
            fwrite(&value, sizeof(value), 1, fp_data);
            current_index++;
        }
        active_id.clear();
        last_time = current_time;
    }
    void sweep_last() { fwrite(&current_index, sizeof(current_index), 1, fp_indptr); }
    void submit(uint64_t time, int64_t id, uint8_t value) {
        if (!first_submit && time != current_time) { sweep_active(); }
        assert(time >= current_time);
        first_submit = false;
        current_time = time;
        // you can use id = -1 to add a new timeframe with no data
        if (id >= 0) {
            if (current_value.size() <= id) { current_value.resize(id + 1); }
            if (!current_value[id].active && current_value[id].value != value) {
                current_value[id].active = true;
                active_id.push_back(id);
            }
            current_value[id].value = value;
        }
    }
};

struct InMemDumper {
    FILE* fp_time;
    FILE* fp_indptr;
    FILE* fp_indices;
    FILE* fp_data;
    InMemDumper(std::string prefix) {
        fp_time = fopen((prefix + ".time.bin").c_str(), "wb");
        fp_indptr = fopen((prefix + ".indptr.bin").c_str(), "wb");
        fp_indices = fopen((prefix + ".indices.bin").c_str(), "wb");
        fp_data = fopen((prefix + ".data.bin").c_str(), "wb");
    }
    ~InMemDumper() {
        if (current_time != last_time) { sweep_active(); }
        sweep_last();
        std::cerr << "DUMP: times=" << times.size() << " indptr=" << indptr.size()
                  << " indices=" << indices.size() << " data=" << data.size() << " toggle_n=" << toggle_N << std::endl;
        fwrite(times.data(), sizeof(*times.data()), times.size(), fp_time);
        fwrite(indptr.data(), sizeof(*indptr.data()), indptr.size(), fp_indptr);
        fwrite(indices.data(), sizeof(*indices.data()), indices.size(), fp_indices);
        fwrite(data.data(), sizeof(*data.data()), data.size(), fp_data);
        fclose(fp_time);
        fclose(fp_indptr);
        fclose(fp_indices);
        fclose(fp_data);
    }
    std::vector<CovInfo> current_value;
    std::vector<uint64_t> active_id;
    bool first_submit = true;
    uint64_t last_time = -1;
    uint64_t current_time = 0;
    std::vector<uint64_t> times;
    std::vector<uint64_t> indptr;
    std::vector<uint64_t> indices;
    std::vector<uint8_t> data;
    uint32_t log_counter = 0;
    void sweep_active() {
        if (log_counter++ >= 1000) {
            std::cerr << "DUMPER: cycle=" << times.size() << " time=" << current_time
                      << " rate=" << std::fixed << std::setprecision(2)
                      << 1.0 * data.size() / times.size() << std::endl;
            log_counter = 0;
        }
        times.push_back(current_time);
        indptr.push_back(indices.size());
        std::sort(active_id.begin(), active_id.end());
        for (auto id : active_id) {
            current_value[id].active = false;
            uint8_t value = current_value[id].value;
            indices.push_back(id);
            data.push_back(value);
        }
        active_id.clear();
    }
    void sweep_last() { indptr.push_back(indices.size()); }
    void submit(uint64_t time, int64_t id, uint8_t value) {
        if (!first_submit && time != current_time) { sweep_active(); }
        assert(time >= current_time);
        first_submit = false;
        current_time = time;
        if (id >= 0) {
            if (current_value.size() <= id) { current_value.resize(id + 1); }
            if (!current_value[id].active && current_value[id].value != value) {
                current_value[id].active = true;
                active_id.push_back(id);
            }
            current_value[id].value = value;
        }
    }
};

struct CovStat {
    uint8_t last_value = 3;
    int64_t total_flip = 0;
    int64_t total_pos = 0;
    int64_t total_neg = 0;
    void reset() {
        total_flip = 0;
        total_pos = 0;
        total_neg = 0;
    }
    void update(uint8_t value) {
        if(value) total_pos++;
        else total_neg++;
        if(last_value != value) total_flip++;
        last_value = value;
    }
};

struct AggDenseDumper {
    FILE* fp_value;
    FILE* fp_flip;
    FILE* fp_time;
    FILE* fp_pos;
    FILE* fp_neg;
    int64_t dump_interval;
    AggDenseDumper(std::string prefix, int64_t dump_interval) {
        fp_time = fopen((prefix + ".time.bin").c_str(), "wb");
        fp_value = fopen((prefix + ".value.bin").c_str(), "wb");
        fp_flip = fopen((prefix + ".flip.bin").c_str(), "wb");
        fp_pos = fopen((prefix + ".pos.bin").c_str(), "wb");
        fp_neg = fopen((prefix + ".neg.bin").c_str(), "wb");
        this->dump_interval = dump_interval;
    }
    ~AggDenseDumper() {
        if(current_time != last_sweep) {
            sweep_all();
        }
        save_all();
    }
    std::vector<CovStat> covs;
    std::vector<uint64_t> times;
    std::vector<std::vector<CovStat>> dumps;
    void save_all() {
        if(dumps.empty()) return;
        size_t maxl = 0;
        for(const auto & cvs: dumps) {
            maxl = std::max(maxl, cvs.size());
        }
        for(auto & cvs: dumps) {
            cvs.resize(maxl);
            for(const auto & d: cvs) {
#define SAVE(x, y) fwrite(&(x), sizeof(x), 1, y)
                SAVE(d.last_value, fp_value);
                SAVE(d.total_flip, fp_flip);
                SAVE(d.total_pos, fp_pos);
                SAVE(d.total_neg, fp_neg);
#undef SAVE
            }
            cvs.clear();
        }
        fwrite(times.data(), sizeof(*times.data()), times.size(), fp_time);
        fclose(fp_time);
        fclose(fp_value);
        fclose(fp_flip);
        fclose(fp_pos);
        fclose(fp_neg);
    }
    uint64_t last_sweep = 0;
    void sweep_all() {
        last_sweep = current_time;
        times.push_back(current_time);
        std::cerr << "DUMP: times=" << current_time << " covs=" << covs.size() << " dumps=" << dumps.size() << " toggle_n=" << toggle_N << std::endl;
        dumps.push_back(covs);
        for(auto & c: covs) c.reset();
    }
    bool first_submit = true;
    uint64_t current_time = 0;
    uint64_t interval_counter = 0;
    void sweep_active() {
        if(++interval_counter >= dump_interval) {
            sweep_all();
            interval_counter = 0;
        }
    }
    void submit(uint64_t time, int64_t id, uint8_t value) {
        if (!first_submit && time != current_time) {
            current_time = time;
            sweep_active();
        }
        first_submit = false;
        current_time = time;
        if(id >= 0) {
            if (covs.size() <= id) { covs.resize(id + 1); }
            covs[id].update(value);
        }
    }
};

struct ToggleInfo {
    std::vector<uint64_t> buf = {};
    void submit(uint64_t time) {
        if(buf.size() < toggle_N) {
            buf.push_back(time);
        }
    }
    void legalize() {
        buf.resize(toggle_N, std::numeric_limits<uint64_t>::max());
    }
};

struct ToggleDumper {
    FILE * fdump;
    std::vector<ToggleInfo> buf;
    ToggleDumper(std::string name) {
        fdump = fopen((name + ".toggle.bin").c_str(), "wb");
    }
    ~ToggleDumper() {
        for(size_t i = 0; i < buf.size(); i++) {
            buf[i].legalize();
            fwrite(buf[i].buf.data(), sizeof(uint64_t), buf[i].buf.size(), fdump);
        }
        fclose(fdump);
    }
    void submit(int point_id, uint64_t time) {
        if(point_id >= buf.size()) {
            buf.resize(point_id + 1);
        }
        buf[point_id].submit(time);
    }
};

}  // namespace

static bool initialize = true;
static std::unique_ptr<AggDenseDumper> dumper;
static std::unique_ptr<ToggleDumper> toggle;

static void initialize_all() {
    const auto * arg = getenv("DUMP");
    const auto * tgn = getenv("TOGGLE_N");
    if(tgn) {
        toggle_N = atoi(tgn);
    }
    const auto * interval_s = getenv("DUMP_INTERVAL");
    uint64_t interval = 10000;
    if(interval_s) interval = atoi(interval_s);
    if (arg) {
        std::cerr << "dump= " << arg << " interval=" << interval << std::endl;
        dumper = std::make_unique<AggDenseDumper>(arg, interval);
        toggle = std::make_unique<ToggleDumper>(arg);
    }
    initialize = false;
}

extern "C" void submit_cov_s(int point_id, uint8_t value, const char*) {
    uint64_t time = Verilated::threadContextp()->time();
    if (initialize) {
        initialize_all();
    }
    if (dumper) { dumper->submit(time, point_id, value); }
}

void submit_cov_toggle(int point_id) {
    uint64_t time = Verilated::threadContextp()->time();
    if (initialize) {
        initialize_all();
    }
    if (toggle) { toggle->submit(point_id, time); }
}