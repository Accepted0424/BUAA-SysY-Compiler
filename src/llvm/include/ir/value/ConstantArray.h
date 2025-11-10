#include <memory>

#include "ConstantData.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/type.h"

class ConstantArray : public ConstantData {
public:
    static std::shared_ptr<ConstantArray> create(TypePtr elementTy,
                                              const std::vector<std::shared_ptr<ConstantInt>> &elements) {
        auto arrayTy = std::make_shared<ArrayType>(elementTy, elements.size());
        return std::make_shared<ConstantArray>(arrayTy, elements);
    }

    ConstantArray(TypePtr type, const std::vector<std::shared_ptr<ConstantInt>> &elements)
        : ConstantData(type), elements_(elements) {}

private:
    std::vector<std::shared_ptr<ConstantInt>> elements_;
};
