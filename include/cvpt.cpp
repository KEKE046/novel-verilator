#include "verilated.h"
#include <iomanip>
#include <iostream>

namespace {

struct CovInfo {
    bool active = false;
    uint8_t value = -1;
};

struct FileDumper {
    FILE * fp_time;
    FILE * fp_indptr;
    FILE * fp_indices;
    FILE * fp_data;
    FileDumper(std::string prefix) {
        fp_time = fopen((prefix + ".time.bin").c_str(), "wb");
        fp_indptr = fopen((prefix + ".indptr.bin").c_str(), "wb");
        fp_indices = fopen((prefix + ".indices.bin").c_str(), "wb");
        fp_data = fopen((prefix + ".data.bin").c_str(), "wb");
    }
    ~FileDumper() {
        if(current_time != last_time) {
            sweep_active();
        }
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
        if(log_counter++ >= 1000) {
            std::cerr << "DUMPER: " << time_counter << " time=" << current_time << " rate=" << std::fixed << std::setprecision(2) << 1.0 * current_index / time_counter << std::endl;
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
    FILE * fp_time;
    FILE * fp_indptr;
    FILE * fp_indices;
    FILE * fp_data;
    InMemDumper(std::string prefix) {
        fp_time = fopen((prefix + ".time.bin").c_str(), "wb");
        fp_indptr = fopen((prefix + ".indptr.bin").c_str(), "wb");
        fp_indices = fopen((prefix + ".indices.bin").c_str(), "wb");
        fp_data = fopen((prefix + ".data.bin").c_str(), "wb");
    }
    ~InMemDumper() {
        if(current_time != last_time) {
            sweep_active();
        }
        sweep_last();
        std::cerr << "DUMP: times=" << times.size() << " indptr=" << indptr.size() << " indices=" << indices.size() << " data=" << data.size() << std::endl;
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
        if(log_counter++ >= 1000) {
            std::cerr << "DUMPER: cycle=" << times.size() << " time=" << current_time << " rate=" << std::fixed << std::setprecision(2) << 1.0 * data.size() / times.size() << std::endl;
            log_counter = 0; 
        }
        times.push_back(current_time);
        indptr.push_back(indices.size());
        std::sort(active_id.begin(), active_id.end());
        for(auto id: active_id) {
            current_value[id].active = false;
            uint8_t value = current_value[id].value;
            indices.push_back(id);
            data.push_back(value);
        }
        active_id.clear();
    }
    void sweep_last() {
        indptr.push_back(indices.size());
    }
    void submit(uint64_t time, int64_t id, uint8_t value) {
        if(!first_submit && time != current_time) {
            sweep_active();
        }
        assert(time >= current_time);
        first_submit = false;
        current_time = time;
        if(id >= 0) {
            if(current_value.size() <= id) {
                current_value.resize(id + 1);
            }
            if(!current_value[id].active && current_value[id].value != value) {
                current_value[id].active = true;
                active_id.push_back(id);
            }
            current_value[id].value = value;
        }
    }
};

} // namespace

static bool initialize = true;
static std::unique_ptr<InMemDumper> dumper;

extern "C" void submit_cov_s(int point_id, uint8_t value, const char*) {
    uint64_t time = Verilated::threadContextp()->time();
    if(initialize) {
        auto arg = getenv("DUMP");
        if(arg) {
            std::cerr << "Dump prefix: " << arg << std::endl;
            dumper = std::make_unique<InMemDumper>(arg);
        }
        initialize = false;
    }
    if (dumper) { dumper->submit(time, point_id, value + 1); }
}
