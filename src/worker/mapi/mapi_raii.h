// WLM2PST - RAII wrappers for COM/MAPI lifetimes (spec section 8).
//
// MAPI lifetime rules encoded here:
//  * Every interface obtained from MAPI must be Release()d before
//    MAPIUninitialize(); MapiPtr enforces scope-bound release.
//  * Memory returned through MAPIAllocateBuffer (rows, prop arrays, entry ids)
//    must go back through MAPIFreeBuffer of the SAME runtime; MapiBuffer binds
//    the pointer to the loaded runtime's free function.
//  * SRowSet rows are freed row-by-row (FreeProws semantics): RowSetGuard.
#pragma once

#include "common/win_compat.h"

#include <mapidefs.h>
#include <mapix.h>

#include <utility>

namespace wlm2pst::mapi {

// Scope-bound COM/MAPI interface pointer (no implicit AddRef on raw attach).
template <typename T>
class MapiPtr {
public:
    MapiPtr() = default;
    MapiPtr(const MapiPtr&) = delete;
    MapiPtr& operator=(const MapiPtr&) = delete;
    MapiPtr(MapiPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    MapiPtr& operator=(MapiPtr&& other) noexcept {
        if (this != &other) { reset(); ptr_ = std::exchange(other.ptr_, nullptr); }
        return *this;
    }
    ~MapiPtr() { reset(); }

    void reset() {
        if (ptr_) { ptr_->Release(); ptr_ = nullptr; }
    }
    // For use as an out-parameter: releases any held interface first.
    T** put() { reset(); return &ptr_; }
    // put() disguised for APIs taking LPUNKNOWN* / void**.
    LPUNKNOWN* put_unknown() { reset(); return reinterpret_cast<LPUNKNOWN*>(&ptr_); }
    void** put_void() { reset(); return reinterpret_cast<void**>(&ptr_); }

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

// MAPIAllocateBuffer-owned memory, freed through the owning runtime.
// Definition of free_fn is bound at construction to avoid a hard link-time
// dependency on mapi32 (all entry points are GetProcAddress-resolved).
class MapiBuffer {
public:
    using FreeFn = LPMAPIFREEBUFFER;  // ULONG (WINAPI*)(LPVOID)

    MapiBuffer() = default;
    explicit MapiBuffer(FreeFn free_fn) : free_fn_(free_fn) {}
    MapiBuffer(const MapiBuffer&) = delete;
    MapiBuffer& operator=(const MapiBuffer&) = delete;
    MapiBuffer(MapiBuffer&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)), free_fn_(other.free_fn_) {}
    MapiBuffer& operator=(MapiBuffer&& other) noexcept {
        if (this != &other) { reset(); ptr_ = std::exchange(other.ptr_, nullptr); free_fn_ = other.free_fn_; }
        return *this;
    }
    ~MapiBuffer() { reset(); }

    void reset() {
        if (ptr_ && free_fn_) { free_fn_(ptr_); }
        ptr_ = nullptr;
    }
    void set_free_fn(FreeFn fn) { free_fn_ = fn; }
    LPVOID* put() { reset(); return &ptr_; }
    template <typename T>
    T** put_as() { reset(); return reinterpret_cast<T**>(&ptr_); }
    template <typename T>
    T* as() const { return static_cast<T*>(ptr_); }
    LPVOID get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    LPVOID ptr_ = nullptr;
    FreeFn free_fn_ = nullptr;
};

// SRowSet returned by IMAPITable::QueryRows: every row buffer must be freed
// individually, then the set itself (classic FreeProws contract).
class RowSetGuard {
public:
    RowSetGuard(LPSRowSet rows, MapiBuffer::FreeFn free_fn) : rows_(rows), free_fn_(free_fn) {}
    RowSetGuard(const RowSetGuard&) = delete;
    RowSetGuard& operator=(const RowSetGuard&) = delete;
    ~RowSetGuard() {
        if (!rows_ || !free_fn_) return;
        for (ULONG i = 0; i < rows_->cRows; ++i) {
            if (rows_->aRow[i].lpProps) free_fn_(rows_->aRow[i].lpProps);
        }
        free_fn_(rows_);
    }
    LPSRowSet get() const noexcept { return rows_; }

private:
    LPSRowSet rows_ = nullptr;
    MapiBuffer::FreeFn free_fn_ = nullptr;
};

// COM apartment guard.
class ComInit {
public:
    ComInit() = default;
    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;
    ~ComInit() { uninitialize(); }

    HRESULT initialize() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // S_FALSE (already initialized on this thread) still requires a
        // matching CoUninitialize; RPC_E_CHANGED_MODE must not.
        initialized_ = SUCCEEDED(hr);
        return hr;
    }
    void uninitialize() {
        if (initialized_) { CoUninitialize(); initialized_ = false; }
    }

private:
    bool initialized_ = false;
};

}  // namespace wlm2pst::mapi
