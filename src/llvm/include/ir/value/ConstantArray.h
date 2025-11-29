#include <memory>

#include "ConstantData.h"
#include "llvm/include/ir/IrForward.h"
#include "llvm/include/ir/type.h"

class ConstantArray : public ConstantData {
public:
    static std::shared_ptr<ConstantArray> create(TypePtr arrayTy,
                                              const std::vector<std::shared_ptr<ConstantInt>> &elements) {
        return std::make_shared<ConstantArray>(arrayTy, elements);
    }

    ConstantArray(TypePtr type, const std::vector<std::shared_ptr<ConstantInt>> &elements)
        : ConstantData(ValueType::ConstantArrayTy, type), elements_(elements) {}

    const std::vector<std::shared_ptr<ConstantInt>> &getElements() const { return elements_; }

private:
    std::vector<std::shared_ptr<ConstantInt>> elements_;
};
