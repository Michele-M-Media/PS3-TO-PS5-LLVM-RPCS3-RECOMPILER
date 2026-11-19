// Diagnostics for exceptions raised while LLVM constructs the native target.
#include <cstdint>
#include <cstdio>
#include <typeinfo>
#include <unwind.h>

namespace
{
struct trace_state
{
    bool active;
    bool saw_throw;
    bool saw_search;
};

thread_local trace_state state{};
}

extern "C" void recompiler_llvm_exception_trace_begin() noexcept
{
    state = {true, false, false};
}

extern "C" void recompiler_llvm_exception_trace_end() noexcept
{
    state = {};
}

extern "C" [[noreturn]] void __real___cxa_throw(void*, std::type_info*, void (*)(void*));
extern "C" _Unwind_Reason_Code __real___gxx_personality_v0(
    int, _Unwind_Action, std::uint64_t, _Unwind_Exception*, _Unwind_Context*);

extern "C" [[noreturn, gnu::noinline]] void __wrap___cxa_throw(
    void* exception, std::type_info* type, void (*destroy)(void*))
{
    if (state.active && !state.saw_throw)
    {
        state.saw_throw = true;
        const void* caller = __builtin_extract_return_addr(__builtin_return_address(0));
        std::fprintf(stderr, "RECOMPILER_LLVM_THROW caller=%p type=%s\n", caller, type ? type->name() : "<null>");
    }
    __real___cxa_throw(exception, type, destroy);
}

extern "C" _Unwind_Reason_Code __wrap___gxx_personality_v0(
    int version, _Unwind_Action actions, std::uint64_t exception_class,
    _Unwind_Exception* exception, _Unwind_Context* context)
{
    if (state.active && !state.saw_search && context && (actions & _UA_SEARCH_PHASE))
    {
        state.saw_search = true;
        const auto first_ip = static_cast<unsigned long long>(_Unwind_GetIP(context));
        std::fprintf(stderr, "RECOMPILER_LLVM_UNWIND ip=0x%llx class=0x%016llx local_throw=%d\n",
            first_ip, static_cast<unsigned long long>(exception_class), state.saw_throw ? 1 : 0);
    }
    return __real___gxx_personality_v0(version, actions, exception_class, exception, context);
}
