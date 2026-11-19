// Route title C++ allocations through the already linked PS5 Platform heap.
// C wrappers keep their real native backing, pointer ownership and libc fallback.
// Preserve C++ new-handler, exception and nothrow contracts; never return a
// null pointer from a throwing allocation or manufacture allocation success.
#include <cstddef>
#include <new>

extern "C" {
void __wrap_free(void*);
int __wrap_posix_memalign(void**,std::size_t,std::size_t);
}

namespace {
void* allocate(std::size_t size,std::size_t alignment=0)
{
    if(!size) size=1;
    if (alignment && (alignment & (alignment - 1)))
        throw std::bad_alloc();
    constexpr std::size_t default_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    static_assert(default_alignment >= alignof(std::max_align_t));
    if (alignment < default_alignment)
        alignment = default_alignment;

    for (;;)
    {
        void* pointer = nullptr;
        if (__wrap_posix_memalign(&pointer, alignment, size) == 0 && pointer)
            return pointer;
        const auto handler=std::get_new_handler();
        if(!handler) throw std::bad_alloc();
        handler();
    }
}
}

void* operator new(std::size_t n) {return allocate(n);}
void* operator new[](std::size_t n) {return allocate(n);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept
{try{return allocate(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept
{try{return allocate(n);}catch(...){return nullptr;}}
void* operator new(std::size_t n,std::align_val_t a)
{return allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a)
{return allocate(n,static_cast<std::size_t>(a));}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept
{try{return allocate(n,static_cast<std::size_t>(a));}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept
{try{return allocate(n,static_cast<std::size_t>(a));}catch(...){return nullptr;}}

void operator delete(void* p) noexcept {__wrap_free(p);}
void operator delete[](void* p) noexcept {__wrap_free(p);}
void operator delete(void* p,std::size_t) noexcept {__wrap_free(p);}
void operator delete[](void* p,std::size_t) noexcept {__wrap_free(p);}
void operator delete(void* p,std::align_val_t) noexcept {__wrap_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {__wrap_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {__wrap_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {__wrap_free(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept {__wrap_free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {__wrap_free(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept {__wrap_free(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept {__wrap_free(p);}
