#include "V3PchAstNoMT.h"

#include "V3Ast.h"
#include "V3FileLine.h"
#include "V3Inst.h"

#include "V3AddHook.h"

#include "V3Const.h"
#include "V3Task.h"
#include <stack>
#include <fstream>
VL_DEFINE_DEBUG_FUNCTIONS;

namespace {

// struct AddTaskVisitor final : public VNVisitor {
//     bool found = false;
//     static AstTask * creatTask(FileLine * flp) {
//         AstVar * const arg1 = new AstVar(flp, VVarType::PORT, "id", VFlagChildDType(), new AstBasicDType(flp, VBasicDTypeKwd::INT));
//         AstVar * const arg2 = new AstVar(flp, VVarType::PORT, "value", VFlagChildDType(), new AstBasicDType(flp, VBasicDTypeKwd::BIT));
//         AstVar * const arg3 = new AstVar(flp, VVarType::PORT, "name", VFlagChildDType(), new AstBasicDType(flp, VBasicDTypeKwd::STRING));
//         arg1->addNext(arg2);
//         arg1->addNext(arg3);
//         arg1->direction(VDirection::INPUT);
//         arg2->direction(VDirection::INPUT);
//         arg3->direction(VDirection::INPUT);
//         AstTask * const res =  new AstTask(flp, "submit_cov_s", arg1);
//         res->dpiImport(true);
//         res->prototype(true);
//         return res;
//     }
//     void visit(AstPackage * nodep) override {
//         if(nodep->name() == "$unit") {
//             found = true;
//             auto * task = creatTask(nodep->fileline());
//             nodep->addStmtsp(task);
//         }
//         iterateChildren(nodep);
//     }
//     void visit(AstNode * nodep) override {
//         iterateChildren(nodep);
//     }
// public:
//     explicit AddTaskVisitor(AstNetlist* nodep) {
//         iterate(nodep);
//         if(!found) {
//             auto * modp = nodep->modulesp();
//             auto * unit = new AstPackage(nodep->fileline(), "$unit");
//             auto * task = creatTask(nodep->fileline());
//             modp->addNextHere(unit);
//             unit->addStmtsp(task);
//             unit->inLibrary(true);
//         }
//     }
//     ~AddTaskVisitor() override = default;
// };

struct CVPTInfo {
    // -1 means top level
    ssize_t parent_index;
    std::string parent_module;
    std::string type;
};

class MatchVisitor final : public VNVisitor {
    const VNUser1InUse m_inuser1;
    std::vector<CVPTInfo> m_cvpt_info;
    std::string m_current_module;
    std::stack<ssize_t> m_parent_index;
    std::stack<AstNodeStmt*> stmt_stack;
    inline AstNodeStmt * current_stmt() {
        return stmt_stack.top();
    }
    AstNode * createCoveragePointStmt(FileLine * flp, AstNodeExpr * condp, ssize_t & index, std::string type) {
        index = static_cast<ssize_t>(m_cvpt_info.size());
        ssize_t parent_index = -1;
        std::string parent_module;
        if(m_parent_index.size() > 0) {
            parent_index = m_parent_index.top();
        } else {
            parent_index = -1;
        }
        m_cvpt_info.push_back({parent_index, m_current_module, std::move(type)});
        AstArg * const arg1 = new AstArg(flp, "", new AstConst(flp, index));
        AstArg * const arg2 = new AstArg(flp, "", condp->cloneTree(false));
        arg1->addNext(arg2);
        AstArg * const arg3 = new AstArg(flp, "", new AstConst(flp, AstConst::String(), "cov"));
        arg1->addNext(arg3);
        AstTaskRef * const new_node = new AstTaskRef(flp, "submit_cov_s", arg1);
        AstStmtExpr * const new_stmt = new AstStmtExpr(flp, new_node);
        return new_stmt;
    }
    void visit(AstNodeStmt * nodep) override {
        stmt_stack.push(nodep);
        iterateChildren(nodep);
        stmt_stack.pop();
    }
    void visit(AstIf * nodep) override {
        stmt_stack.push(nodep);
        // std::cout << "Set user1p " << std::endl;
        ssize_t index = 0;
        current_stmt()->user1p(createCoveragePointStmt(nodep->fileline(), nodep->condp(), index, "if"));
        m_parent_index.push(index);
        iterateChildren(nodep);
        m_parent_index.pop();
        stmt_stack.pop();
    }
    void visit(AstNodeModule * nodep) override {
        m_current_module = nodep->name();
        iterateChildren(nodep);
        m_current_module = "";
    }
    void visit(AstNodeCond * nodep) override {
        ssize_t index = 0;
        auto * new_stmt = createCoveragePointStmt(nodep->fileline(), nodep->condp(), index, "mux");
        if(current_stmt()->user1p()) {
            current_stmt()->dump();
            current_stmt()->user1p()->addNextHere(new_stmt);
        } else {
            current_stmt()->user1p(new_stmt);
        }
        m_parent_index.push(index);
        iterateChildren(nodep);
        m_parent_index.pop();
    }
    void visit(AstNode * nodep) override {
        iterateChildren(nodep);
    }
public:
    explicit MatchVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~MatchVisitor() override = default;
    void dump_csv(std::ostream & out) {
        size_t index = 0;
        for(auto & info: m_cvpt_info) {
            out << index << "\t" << info.parent_index << "\t" << info.parent_module << "\t" << info.type << "\n"; 
            index++;
        }
    }
};

class RewriteVisitor final : public VNVisitor {
    bool inside_always = false;
    void visit(AstAlways * nodep) override {
        inside_always = true;
        iterateChildren(nodep);
        inside_always = false;
    }
    void visit(AstNode * nodep) override {
        // std::cout << "RewriteVisitor: " << nodep->type() << " " << nodep->name() << " user1p=" << nodep->user1p() << std::endl;
        if(nodep->user1p()) {
            auto * target = nodep->user1p();
            nodep->user1p(nullptr);
            if(!inside_always) {
                target = new AstAlways(target->fileline(), VAlwaysKwd::ALWAYS_COMB, nullptr, new AstBegin(target->fileline(), "", target));
            }
            nodep->addNextHere(target);
        }
        iterateChildren(nodep);
    }
public:
    explicit RewriteVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~RewriteVisitor() override = default;
};

struct CovPointInfo {
    FileLine * flp;
    std::string inst_path;
    size_t original_index;
};

class RenumberHookVisitor final: public VNVisitor {
    std::vector<CovPointInfo> m_cov_points;

    void visit(AstTaskRef * nodep) override {
        if(nodep->name() == "submit_cov_s") {
            auto * indexp = static_cast<AstConst*>(static_cast<AstArg*>(nodep->pinsp())->exprp());
            auto * namep = static_cast<AstConst*>(static_cast<AstArg*>(nodep->pinsp()->nextp()->nextp())->exprp());
            auto name = namep->num().toString();
            auto index = m_cov_points.size();
            auto original_index = indexp->num().toUInt();
            indexp->num().setLong(index);
            m_cov_points.push_back({nodep->fileline(), name, original_index});
        }
    }

    void visit(AstNode * nodep) override {
        iterateChildren(nodep);
    }
public:
    explicit RenumberHookVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~RenumberHookVisitor() override = default;
    void dump_csv(std::ostream & out) {
        ssize_t i = 0;
        for(auto & info: m_cov_points) {
            out << i << "\t" << info.flp->filename() << ":" << info.flp->firstLineno() << ":" << info.flp->firstColumn() << "\t";
            out << info.inst_path << "\t" << info.original_index << "\n";
            i++;
        }
    }
};

} // namespace

void V3AddHook::addHook(AstNetlist* nodep, const std::string & dump_path) {
    UINFO(2, __FUNCTION__ << ": " << endl);
    // AddTaskVisitor addtask{nodep};
    MatchVisitor match {nodep};
    RewriteVisitor rewrite {nodep};
    std::ofstream out(dump_path);
    match.dump_csv(out);
    out.close();
    // V3Global::dumpCheckGlobalTree("addHook", 0, dumpTreeEitherLevel() >= 3);
}

void V3AddHook::renumberHook(AstNetlist* nodep, const std::string & dump_path) {
    UINFO(2, __FUNCTION__ << ": " << endl);
    RenumberHookVisitor renumber {nodep};
    std::ofstream out(dump_path);
    renumber.dump_csv(out);
    out.close();
}