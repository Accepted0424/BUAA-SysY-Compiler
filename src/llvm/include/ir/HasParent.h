#pragma once

template <typename Ty> class HasParent {
public:
    using TyPtr = Ty *;

    virtual ~HasParent() = default;

    void SetParent(TyPtr parent) {
        parent_ = parent;
    }
    
    void RemoveParent() {
        parent_ = nullptr;
    }
    
    TyPtr Parent() const { return parent_; }

protected:
    HasParent(TyPtr parent = nullptr) : parent_(parent) {}

private:
    TyPtr parent_;
};
