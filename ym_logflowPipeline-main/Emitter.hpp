#pragma once

template <typename T>
class Emitter {
public:
    virtual ~Emitter() {}
    virtual void emit(const T& item) = 0;
};
