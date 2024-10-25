#include "V3PchAstNoMT.h"

#include "V3Inst.h"

#include "V3Const.h"

VL_DEFINE_DEBUG_FUNCTIONS;

class MuxCovVisitor final : public VNVisitor {
    // void visit()
    // void visit(AstNode * nodep) override {
    //     iterateChildren(nodep);
    // }
public:
    explicit MuxCovVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~MuxCovVisitor() override = default;
};