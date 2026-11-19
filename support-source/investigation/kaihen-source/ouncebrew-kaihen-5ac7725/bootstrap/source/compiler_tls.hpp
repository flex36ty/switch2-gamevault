// SPDX-License-Identifier: GPL-2.0-only
#pragma once
extern "C" {
#include <switch/arm/tls.h>
}

namespace ounce {

// A scoped compiler-TLS block for the bootstrap's flat libounce calls.
// No SDK runtime or libounce thread/managed-session API may run in scope.
// The linker checks that the linked TLS is zero-initialized and fits here.
class CompilerTlsScope {
  public:
    CompilerTlsScope() {
        slot_ = reinterpret_cast<void* volatile*>(static_cast<u8*>(armGetTls()) + 0x1f8);
        previous_ = *slot_;
        *slot_ = storage_;
    }

    ~CompilerTlsScope() {
        *slot_ = previous_;
    }

    // pthread functions read the same TLS slot as a thread-object
    // pointer. Suspend compiler TLS while calling into that runtime.
    void suspend() {
        *slot_ = previous_;
    }

    void resume() {
        *slot_ = storage_;
    }

    CompilerTlsScope(const CompilerTlsScope&) = delete;
    CompilerTlsScope& operator=(const CompilerTlsScope&) = delete;

  private:
    // Two-pointer AArch64 TCB prefix followed by the linked TLS image.
    alignas(16) u8 storage_[0x100] = {};
    void* volatile* slot_;
    void* previous_;
};

} // namespace ounce
