#include "verilated.h"

namespace {

class Dumper {
    FILE * fp_time;
    FILE * fp_indptr;
    FILE * fp_indices;
    FILE * fp_data;
    bool first_submit = true;
    uint64_t last_time = -1;
    uint64_t current_time = 0;
    uint64_t current_index = 0;
    struct CovInfo {
        bool active = false;
        uint8_t value = -1;
    };
    std::vector<CovInfo> current_value;
    std::vector<uint64_t> active_id;
public:
    explicit Dumper(const std::string &prefix) {
        fp_time = fopen((prefix + ".time.bin").c_str(), "wb");
        fp_indptr = fopen((prefix + ".indptr.bin").c_str(), "wb");
        fp_indices = fopen((prefix + ".indices.bin").c_str(), "wb");
        fp_data = fopen((prefix + ".data.bin").c_str(), "wb");
    }
    ~Dumper() {
        if(current_time != last_time) {
            sweep_active();
        }
        sweep_last();
        fclose(fp_time);
        fclose(fp_indptr);
        fclose(fp_indices);
        fclose(fp_data);
    }
    void sweep_first() {
        fwrite(&current_index, sizeof(current_index), 1, fp_indptr);
    }
    void sweep_active() {
        // std::cout << "sweep active: " << current_time << std::endl;
        fwrite(&current_time, sizeof(current_time), 1, fp_time);
        fwrite(&current_index, sizeof(current_index), 1, fp_indptr);
        std::sort(active_id.begin(), active_id.end());
        for(auto id: active_id) {
            current_value[id].active = false;
            uint8_t value = current_value[id].value;
            fwrite(&id, sizeof(id), 1, fp_indices);
            fwrite(&value, sizeof(value), 1, fp_data);
            current_index ++;
        }
        active_id.clear();
        last_time = current_time;
    }
    void sweep_last() {
        fwrite(&current_index, sizeof(current_index), 1, fp_indptr);
    }
    void submit(uint64_t time, int64_t id, uint8_t value) {
        if(!first_submit && time != current_time) {
            sweep_active();
        }
        assert(time >= current_time);
        first_submit = false;
        current_time = time;
        // you can use id = -1 to add a new timeframe with no data
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
static std::unique_ptr<Dumper> dumper;

extern "C" void submit_cov_s(int point_id, uint8_t value, const char*) {
    uint64_t time = Verilated::threadContextp()->time();
    if(initialize) {
        auto * arg = getenv("DUMP");
        if(arg) {
            dumper = std::make_unique<Dumper>(arg);
        }
        initialize = false;
    }
    if(dumper) {
        dumper->submit(time, point_id, value + 1);
    }
}
