#ifndef VERILATOR_V3ADDHOOK_H_
#define VERILATOR_V3ADDHOOK_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3Ast.h"

class V3AddHook final {
public:
    static void addHook(AstNetlist* nodep, const std::string& dump_path);
    static void renumberHook(AstNetlist* nodep, const std::string& dump_path);
};

#endif